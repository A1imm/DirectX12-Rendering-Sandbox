#include "Renderwidget.h"


RenderWidget::RenderWidget(unsigned int width, unsigned int height, HWND hWnd)
	:m_width(width), m_height(height), m_hWnd(hWnd)
{
	Initialize();
}

void RenderWidget::Initialize()
{
#if defined(DEBUG) || defined(_DEBUG) 
	// Enable the D3D12 debug layer.
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
		const HRESULT dresult = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
		assert(SUCCEEDED(dresult));
		debugController->EnableDebugLayer();
	}
#endif

	//1. Configure directx and create all required objects
	CreateDXDeviceAndFactory();
	CreateCommandObjects();
	CreateSwapChain(m_width, m_height);
	CreateDescriptorHeaps();

	//2. Create shaders and resources
	CreateWorldViewProjectionMatrixBuffer();
	CompileShaders();
	LoadGeometry();
	LoadTexture(L"height_map.png");

	//3. Initialize Graphic Pipeline
	BuildRootSignature();
	CreateGraphicPipeline();

	//4. Execute all commands
	ExecuteCommandList();
	FlushCommandQueue();

	//5. Create buffers
	Resize(m_width, m_height);

	//6. Initialize the world view projection matrix
	UpdateWorldViewProjectionBuffer();
}

void RenderWidget::Resize(int width, int height)
{
	HRESULT result = m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr);
	assert(SUCCEEDED(result));

	ResizeSwapChain(width, height);
	CreateRenderTargetView();
	CreateDepthStencilView(width, height);

	// Transition the resource from its initial state to be used as a depth buffer.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_depthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Execute the resize commands.
	ExecuteCommandList();

	// Wait until resize is complete.
	FlushCommandQueue();

	//Update projection matrix and viewport
	m_camera.Width = static_cast<float>(width);
	m_camera.Height = static_cast<float>(height);
	m_camera.UpdateProjetionMatrix();
	UpdateViewport(width, height);

	m_width = width;
	m_height = height;
}

void RenderWidget::CreateDXDeviceAndFactory()
{
	const HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory));
	assert(SUCCEEDED(result));

	// Try to create hardware device.
	HRESULT hardwareResult = D3D12CreateDevice(
		nullptr, // default adapter
		D3D_FEATURE_LEVEL_11_0, //DirectX 11.0 feature level
		IID_PPV_ARGS(&m_dxDevice));

	// Fallback to WARP device.
	if (FAILED(hardwareResult))
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
		hardwareResult = m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter));
		assert(SUCCEEDED(hardwareResult));

		hardwareResult = D3D12CreateDevice(
			pWarpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0, //DirectX 11.0
			IID_PPV_ARGS(&m_dxDevice));

		ThrowIfFailed(hardwareResult);
	}
}

void RenderWidget::CreateCommandObjects()
{
	//Create DirectX Fence
	ThrowIfFailed(
		m_dxDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&m_fence)));

	//Create DirectX Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(
		m_dxDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	//Create DirectX Command Allocator
	ThrowIfFailed(
		m_dxDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		IID_PPV_ARGS(m_directCmdListAlloc.GetAddressOf())));

	//Create DirectX Command List
	ThrowIfFailed(m_dxDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_directCmdListAlloc.Get(),
		nullptr, // Initial PipelineStateObject
		IID_PPV_ARGS(m_commandList.GetAddressOf())));

	m_commandList->Close();
	ResetCommandList();
}

void RenderWidget::ResetCommandList(ID3D12PipelineState* pipelineState)
{
	HRESULT result = m_commandList->Reset(m_directCmdListAlloc.Get(), pipelineState);
	assert(SUCCEEDED(result));
}

void RenderWidget::CreateDescriptorHeaps()
{
	// Render Target View heap descriptor
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	HRESULT result = m_dxDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(m_rtvDescriptorHeap.GetAddressOf()));
	assert(SUCCEEDED(result) && "Can't create the render target view heap descriptor");
	ThrowIfFailed(result);

	// Depth Stencil View heap descriptor
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	result = m_dxDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_dsvDescriptorHeap.GetAddressOf()));
	assert(SUCCEEDED(result) && "Can't create the depth stencil view heap descriptor");
	ThrowIfFailed(result);

	// Shader Resource View heap descriptor
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 2;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	result = m_dxDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvDescriptorHeap));
	assert(SUCCEEDED(result) && "Can't create the shader resource view heap descriptor");
	ThrowIfFailed(result);
}

void RenderWidget::CreateSwapChain(unsigned int width, unsigned int height)
{
	// Release the previous swapchain we will be recreating.
	m_swapChain.Reset();

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = width;
	swapChainDesc.BufferDesc.Height = height;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = BackBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	//DirectX 12 doesn't support MSAA swap chains, instead you shoud create MSAA render target and MSAA depth stencil view
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = SwapChainBufferCount;
	swapChainDesc.OutputWindow = m_hWnd;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


	// Note: Swap chain uses queue to perform flush.
	const HRESULT result = m_dxgiFactory->CreateSwapChain(
		m_commandQueue.Get(),
		&swapChainDesc,
		m_swapChain.GetAddressOf());
	assert(SUCCEEDED(result));
	ThrowIfFailed(result);
}

void RenderWidget::FlushCommandQueue()
{
	m_currentFence++;

	m_commandQueue->Signal(m_fence.Get(), m_currentFence);
	
	const auto completedValue = m_fence->GetCompletedValue();
	assert(completedValue != UINT64_MAX);
	if (completedValue < m_currentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		m_fence->SetEventOnCompletion(m_currentFence, eventHandle);
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
	assert(m_fence->GetCompletedValue() != UINT64_MAX);
}

void RenderWidget::ExecuteCommandList()
{
	HRESULT result = m_commandList->Close();
	assert(SUCCEEDED(result));
	ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
}

void RenderWidget::ResizeSwapChain(unsigned int width, unsigned int height)
{
	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		m_SwapChainBuffer[i].Reset();
	m_depthStencilBuffer.Reset();

	// Resize the swap chain.
	const HRESULT result = m_swapChain->ResizeBuffers(
		SwapChainBufferCount,
		width, height,
		BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
	assert(SUCCEEDED(result));

	m_currBackBuffer = 0;
}

ID3D12Resource* RenderWidget::GetCurrentBackBuffer()const
{
	return m_SwapChainBuffer[m_currBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderWidget::GetCurrentBackBufferView()const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(
		m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		m_currBackBuffer,
		m_rtvDescriptorSize);
}

void RenderWidget::CreateRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	m_rtvDescriptorSize = m_dxDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		const HRESULT result = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i]));
		assert(SUCCEEDED(result));
		m_dxDevice->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, m_rtvDescriptorSize);
	}
}

void RenderWidget::CreateDepthStencilView(unsigned int width, unsigned int height)
{
	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = width;
	depthStencilDesc.Height = height;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	depthStencilDesc.SampleDesc.Count =  1;
	depthStencilDesc.SampleDesc.Quality =  0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	const HRESULT result = m_dxDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(m_depthStencilBuffer.GetAddressOf()));
	assert(SUCCEEDED(result));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension =  D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DepthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	auto depthStencilViewHandle = m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	m_dxDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, depthStencilViewHandle);
	m_depthStencilBuffer->SetName(L"Depth Stencil View");
}

void RenderWidget::UpdateViewport(unsigned int width, unsigned int height)
{
	m_screenViewport.TopLeftX = 0;
	m_screenViewport.TopLeftY = 0;
	m_screenViewport.Width = static_cast<float>(width);
	m_screenViewport.Height = static_cast<float>(height);
	m_screenViewport.MinDepth = 0.0f;
	m_screenViewport.MaxDepth = 1.0f;

	m_scissorRect = { 0, 0, static_cast<long>(width), static_cast<long>(height) };
}

void RenderWidget::CreateWorldViewProjectionMatrixBuffer()
{
	//constant  buffer
	const auto elementByteSize = DirectXHelper::CalcConstantBufferByteSize( sizeof(ObjectConstants));
	const auto elements = 1;

	ThrowIfFailed(m_dxDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(elementByteSize * elements),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_cbWVProjectionMatrix)));

	ThrowIfFailed(m_cbWVProjectionMatrix->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedData)));
}


void RenderWidget::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE resourceTable[2];
	resourceTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	resourceTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsDescriptorTable(2, resourceTable);

	const CD3DX12_STATIC_SAMPLER_DESC sampler(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,   // filter
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_MIRROR); // addressW

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	assert(SUCCEEDED(hr));

	HRESULT result = m_dxDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(m_rootSignature.GetAddressOf()));
	assert(SUCCEEDED(hr));
}

void RenderWidget::CompileShaders()
{
	m_vertexShaderByteCode = DirectXHelper::CompileShader(L"shader.fx", nullptr, "VS_Main", "vs_5_0");
	assert(m_vertexShaderByteCode);

	m_pixelShaderByteCode = DirectXHelper::CompileShader(L"shader.fx", nullptr, "PS_Main", "ps_5_0");
	assert(m_pixelShaderByteCode);

	m_hullShaderByteCode = DirectXHelper::CompileShader(L"shader.fx", nullptr, "HS_Main", "hs_5_0");
	assert(m_hullShaderByteCode);

	m_domainShaderByteCode = DirectXHelper::CompileShader(L"shader.fx", nullptr, "DS_Main", "ds_5_0");
	assert(m_domainShaderByteCode);
}

void RenderWidget::LoadVertexBuffer(const Geometry::VertexBuffer& vertices)
{
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Geometry::Vertex);
	VertexBuffer.VertexByteStride = sizeof(Geometry::Vertex);
	VertexBuffer.VertexBufferByteSize = vbByteSize;
	VertexBuffer.NumberOfVertices = vertices.size();

	// Create the destination buffer
	HRESULT result = m_dxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vbByteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&VertexBuffer.VertexBufferGPU));
	assert(SUCCEEDED(result));

	result = VertexBuffer.VertexBufferGPU->SetName(L"VertexBufferGPU");
	assert(SUCCEEDED(result));

	// Create the upload buffer
	result = m_dxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vbByteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&VertexBuffer.VertexBufferUploader));
	assert(SUCCEEDED(result));

	result = VertexBuffer.VertexBufferUploader->SetName(L"VertexBufferUploader");
	assert(SUCCEEDED(result));

	// Describe the data we want to copy into the default buffer.
	const void* pdata = vertices.data();
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = pdata;
	subResourceData.RowPitch = vbByteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;
	
	// Load vertices to GPU
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(VertexBuffer.VertexBufferGPU.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	UpdateSubresources<1>(m_commandList.Get(), VertexBuffer.VertexBufferGPU.Get(), VertexBuffer.VertexBufferUploader.Get(), 0, 0, 1, &subResourceData);

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(VertexBuffer.VertexBufferGPU.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void RenderWidget::LoadGeometry()
{
	const auto vertices = Geometry::CreateQuadPatchGeometry();
	LoadVertexBuffer(vertices);
}

void RenderWidget::LoadTexture(const wchar_t* path)
{
	auto&&[buffer, width, height] = DirectXHelper::LoadTextureToBuffer(path);
	if (buffer == nullptr)
	{
		assert(false && "Something is wrong with the file");
		return;
	}

	// Create texture
	D3D12_RESOURCE_DESC texturedesc = {};
	texturedesc.Width = width;
	texturedesc.Height = height;
	texturedesc.MipLevels = static_cast<UINT16>(1);
	texturedesc.DepthOrArraySize = 1;
	texturedesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texturedesc.SampleDesc.Count = 1;
	texturedesc.SampleDesc.Quality = 0;
	texturedesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	texturedesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texturedesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	
	HRESULT result = m_dxDevice->CreateCommittedResource(
		&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texturedesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&m_textureResource));
	assert(SUCCEEDED(result));
	m_textureResource->SetName(L"Texture");

	//Create Upload resource
	const auto uploadBufferSize = GetRequiredIntermediateSize(m_textureResource.Get(), 0, 1);
	const CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	result = m_dxDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_textureResourceUpload));
	assert(SUCCEEDED(result));
	m_textureResourceUpload->SetName(L"Upload texture buffer");

	//Calculate image byte size
	constexpr unsigned int bytesPerPixel = 32;//Image format is DXGI_FORMAT_R8G8B8A8_UNORM so each row is 32 bytes
	const unsigned int rowBytesSize = width * bytesPerPixel;
	const unsigned int byteSize = height * rowBytesSize;
	// Describe the data we want to copy into the default buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = buffer.get();
	subResourceData.RowPitch = rowBytesSize;
	subResourceData.SlicePitch = byteSize;


	//Load the image to the newly created resource
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_textureResource.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
	auto d = UpdateSubresources<1>(m_commandList.Get(), m_textureResource.Get(), m_textureResourceUpload.Get(), 0, 0, 1, &subResourceData);
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_textureResource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// 3. Shader resouce view
	//Texture is a resouce so it requires a view
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = m_textureResource->GetDesc().Format;  //srvDesc.Format = m_textureResource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = m_textureResource->GetDesc().MipLevels; //srvDesc.Texture2D.MipLevels = m_textureResource->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	m_dxDevice->CreateShaderResourceView(m_textureResource.Get(), &srvDesc, hDescriptor);
}

void RenderWidget::CreateGraphicPipeline()
{
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_inputLayout = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { m_inputLayout.data(), (UINT)m_inputLayout.size() };
	psoDesc.pRootSignature = m_rootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(m_vertexShaderByteCode->GetBufferPointer()),
		m_vertexShaderByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(m_pixelShaderByteCode->GetBufferPointer()),
		m_pixelShaderByteCode->GetBufferSize()
	};
	psoDesc.HS =
	{
		reinterpret_cast<BYTE*>(m_hullShaderByteCode->GetBufferPointer()),
		m_hullShaderByteCode->GetBufferSize()
	};
	psoDesc.DS =
	{
		reinterpret_cast<BYTE*>(m_domainShaderByteCode->GetBufferPointer()),
		m_domainShaderByteCode->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	psoDesc.RasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = BackBufferFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DepthStencilFormat;
	ThrowIfFailed(m_dxDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}

void RenderWidget::UpdateWorldViewProjectionBuffer()
{
	DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&Geometry::Identity4x4());

	m_camera.UpdateViewMatrix();
	DirectX::XMMATRIX view = m_camera.GetViewMatrix();
	DirectX::XMMATRIX proj = m_camera.GetProjectionMatrix();
	DirectX::XMMATRIX worldViewProj = world * view * proj;

	// Update the constant buffer with the latest worldViewProj matrix.
	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	objConstants.camera = m_camera.GetCameraPos();


	//Upload buffer
	memcpy(m_mappedData, &objConstants, sizeof(objConstants));

}

void RenderWidget::Draw()
{
	m_directCmdListAlloc->Reset();
	ResetCommandList(m_pipelineState.Get());
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and the depth buffer.
	auto depthStencilViewHandle = m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	m_commandList->ClearRenderTargetView(GetCurrentBackBufferView(), DirectX::Colors::LightSteelBlue, 0, nullptr);
	m_commandList->ClearDepthStencilView(depthStencilViewHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Set root signature
	ID3D12DescriptorHeap* descriptorHeaps[] = { m_srvDescriptorHeap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
	m_commandList->SetGraphicsRootConstantBufferView(0, m_cbWVProjectionMatrix->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE textureDescriptorHandle1(m_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	m_commandList->SetGraphicsRootDescriptorTable(1, textureDescriptorHandle1);

	// Input Assembly stage
	m_commandList->IASetVertexBuffers(0, 1, &VertexBuffer.VertexBufferView());
	m_commandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);


	// Rasterizer stage
	m_commandList->RSSetViewports(1, &m_screenViewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Output merger stage
	m_commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &depthStencilViewHandle);

	// Draw Geometry
	m_commandList->DrawInstanced(
		VertexBuffer.NumberOfVertices,
		1, 0, 0);

	// Indicate a state transition on the resource usage.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	// swap the back and front buffers
	ThrowIfFailed(m_swapChain->Present(0, 0));
	m_currBackBuffer = (m_currBackBuffer + 1) % SwapChainBufferCount;

	FlushCommandQueue();
}
#pragma endregion