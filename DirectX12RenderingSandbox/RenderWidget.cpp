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
	LoadTexture(
		L"WoodCrate.png",
		m_cubeTexture,
		0
	);

	LoadTexture(
		L"height_map.png",
		m_terrainHeightMap,
		1
	);

	LoadTexture(
		L"grass-02.png",
		m_terrainTexture,
		2
	);

	//3. Initialize Graphic Pipeline
	BuildRootSignature();
	CreateGraphicPipelines();

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
	srvHeapDesc.NumDescriptors = 3;
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
	m_objectConstantBufferByteSize =
		DirectXHelper::CalcConstantBufferByteSize(
			sizeof(ObjectConstants)
		);

	constexpr UINT objectCount = 2;

	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(
				D3D12_HEAP_TYPE_UPLOAD
			),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(
				m_objectConstantBufferByteSize *
				objectCount
			),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(
				&m_cbWVProjectionMatrix
			)
		)
	);

	ThrowIfFailed(
		m_cbWVProjectionMatrix->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(
				&m_mappedData
				)
		)
	);
}


void RenderWidget::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE resourceTable;
	resourceTable.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		3,
		0
	);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsDescriptorTable(
		1,
		&resourceTable
	);

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
    // Basic pipeline
    m_basicVertexShaderByteCode =
        DirectXHelper::CompileShader(
            L"Basic.hlsl",
            nullptr,
            "VS_Main",
            "vs_5_0"
        );

    assert(m_basicVertexShaderByteCode);

    m_basicPixelShaderByteCode =
        DirectXHelper::CompileShader(
            L"Basic.hlsl",
            nullptr,
            "PS_Main",
            "ps_5_0"
        );

    assert(m_basicPixelShaderByteCode);

    // Tessellation pipeline
    m_tessVertexShaderByteCode =
        DirectXHelper::CompileShader(
            L"Tessellation.hlsl",
            nullptr,
            "VS_Main",
            "vs_5_0"
        );

    assert(m_tessVertexShaderByteCode);

    m_tessPixelShaderByteCode =
        DirectXHelper::CompileShader(
            L"Tessellation.hlsl",
            nullptr,
            "PS_Main",
            "ps_5_0"
        );

    assert(m_tessPixelShaderByteCode);

    m_hullShaderByteCode =
        DirectXHelper::CompileShader(
            L"Tessellation.hlsl",
            nullptr,
            "HS_Main",
            "hs_5_0"
        );

    assert(m_hullShaderByteCode);

    m_domainShaderByteCode =
        DirectXHelper::CompileShader(
            L"Tessellation.hlsl",
            nullptr,
            "DS_Main",
            "ds_5_0"
        );

    assert(m_domainShaderByteCode);
}

void RenderWidget::LoadVertexBuffer(const Geometry::VertexBuffer& vertices, MeshBuffer& mesh)
{
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Geometry::Vertex);
	mesh.VertexByteStride = sizeof(Geometry::Vertex);
	mesh.VertexBufferByteSize = vbByteSize;
	mesh.NumberOfVertices = vertices.size();

	// Create the destination buffer
	HRESULT result = m_dxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vbByteSize),
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mesh.VertexBufferGPU));
	assert(SUCCEEDED(result));

	result = mesh.VertexBufferGPU->SetName(L"VertexBufferGPU");
	assert(SUCCEEDED(result));

	// Create the upload buffer
	result = m_dxDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vbByteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mesh.VertexBufferUploader));
	assert(SUCCEEDED(result));

	result = mesh.VertexBufferUploader->SetName(L"VertexBufferUploader");
	assert(SUCCEEDED(result));

	// Describe the data we want to copy into the default buffer.
	const void* pdata = vertices.data();
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = pdata;
	subResourceData.RowPitch = vbByteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;
	
	// Load vertices to GPU
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mesh.VertexBufferGPU.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	UpdateSubresources<1>(m_commandList.Get(), mesh.VertexBufferGPU.Get(), mesh.VertexBufferUploader.Get(), 0, 0, 1, &subResourceData);

	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mesh.VertexBufferGPU.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void RenderWidget::LoadIndexBuffer(const Geometry::IndexBuffer& indices, MeshBuffer& mesh)
{
	const UINT ibByteSize =
		static_cast<UINT>(
			indices.size() * sizeof(std::uint16_t)
			);

	mesh.IndexBufferByteSize =
		ibByteSize;

	mesh.NumberOfIndices =
		static_cast<UINT>(indices.size());

	// GPU buffer
	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(
				D3D12_HEAP_TYPE_DEFAULT
			),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(
				ibByteSize
			),
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(
				&mesh.IndexBufferGPU
			)
		)
	);

	mesh.IndexBufferGPU->SetName(
		L"IndexBufferGPU"
	);

	// Upload buffer
	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(
				D3D12_HEAP_TYPE_UPLOAD
			),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(
				ibByteSize
			),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(
				&mesh.IndexBufferUploader
			)
		)
	);

	mesh.IndexBufferUploader->SetName(
		L"IndexBufferUploader"
	);

	D3D12_SUBRESOURCE_DATA indexData = {};

	indexData.pData = indices.data();
	indexData.RowPitch = ibByteSize;
	indexData.SlicePitch = ibByteSize;

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			mesh.IndexBufferGPU.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		)
	);

	UpdateSubresources<1>(
		m_commandList.Get(),
		mesh.IndexBufferGPU.Get(),
		mesh.IndexBufferUploader.Get(),
		0,
		0,
		1,
		&indexData
	);

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			mesh.IndexBufferGPU.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_GENERIC_READ
		)
	);
}

void RenderWidget::LoadGeometry()
{
	// Basic pipeline geometry - cube
	const auto cubeVertices =
		Geometry::CreateCubeGeometry();

	const auto cubeIndices =
		Geometry::CreateCubeIndices();

	LoadVertexBuffer(
		cubeVertices,
		m_basicMesh
	);

	LoadIndexBuffer(
		cubeIndices,
		m_basicMesh
	);


	// Tessellation pipeline geometry - quad patch
	const auto terrainVertices =
		Geometry::CreateQuadPatchGeometry();

	LoadVertexBuffer(
		terrainVertices,
		m_tessellationMesh
	);
}

void RenderWidget::LoadTexture(
	const wchar_t* path,
	TextureResource& texture,
	UINT descriptorIndex)
{
	auto&& [buffer, width, height] =
		DirectXHelper::LoadTextureToBuffer(path);

	if (buffer == nullptr)
	{
		assert(false && "Something is wrong with the texture file");
		return;
	}

	// Texture resource
	D3D12_RESOURCE_DESC textureDesc = {};

	textureDesc.Width = width;
	textureDesc.Height = height;
	textureDesc.MipLevels = 1;
	textureDesc.DepthOrArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

	const CD3DX12_HEAP_PROPERTIES defaultHeapProperties(
		D3D12_HEAP_TYPE_DEFAULT
	);

	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&texture.Resource)
		)
	);

	// Upload resource
	const UINT64 uploadBufferSize =
		GetRequiredIntermediateSize(
			texture.Resource.Get(),
			0,
			1
		);

	const CD3DX12_HEAP_PROPERTIES uploadHeapProperties(
		D3D12_HEAP_TYPE_UPLOAD
	);

	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(
				uploadBufferSize
			),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&texture.UploadResource)
		)
	);

	constexpr UINT bytesPerPixel = 4;

	const UINT rowBytesSize =
		width * bytesPerPixel;

	const UINT byteSize =
		height * rowBytesSize;

	D3D12_SUBRESOURCE_DATA subResourceData = {};

	subResourceData.pData =
		buffer.get();

	subResourceData.RowPitch =
		rowBytesSize;

	subResourceData.SlicePitch =
		byteSize;

	// Upload texture
	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Resource.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		)
	);

	UpdateSubresources<1>(
		m_commandList.Get(),
		texture.Resource.Get(),
		texture.UploadResource.Get(),
		0,
		0,
		1,
		&subResourceData
	);

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Resource.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE
		)
	);

	// Create SRV in selected heap slot
	const UINT descriptorSize =
		m_dxDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		);

	CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(
		m_srvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart(),
		descriptorIndex,
		descriptorSize
	);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.Format =
		texture.Resource->GetDesc().Format;

	srvDesc.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDesc.Texture2D.MostDetailedMip = 0;

	srvDesc.Texture2D.MipLevels =
		texture.Resource->GetDesc().MipLevels;

	srvDesc.Texture2D.ResourceMinLODClamp =
		0.0f;

	m_dxDevice->CreateShaderResourceView(
		texture.Resource.Get(),
		&srvDesc,
		descriptorHandle
	);
}

void RenderWidget::CreateGraphicPipelines()
{
	std::vector<D3D12_INPUT_ELEMENT_DESC> basicInputLayout =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"NORMAL",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			12,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			24,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

	std::vector<D3D12_INPUT_ELEMENT_DESC> tessellationInputLayout =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			24,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC basicPsoDesc = {};

	basicPsoDesc.InputLayout =
	{
		basicInputLayout.data(),
		static_cast<UINT>(basicInputLayout.size())
	};

    basicPsoDesc.pRootSignature = m_rootSignature.Get();

    basicPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(
            m_basicVertexShaderByteCode->GetBufferPointer()
        ),
        m_basicVertexShaderByteCode->GetBufferSize()
    };

    basicPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(
            m_basicPixelShaderByteCode->GetBufferPointer()
        ),
        m_basicPixelShaderByteCode->GetBufferSize()
    };

    basicPsoDesc.RasterizerState =
        CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    basicPsoDesc.RasterizerState.FillMode =
        D3D12_FILL_MODE_SOLID;

    basicPsoDesc.RasterizerState.CullMode =
        D3D12_CULL_MODE_NONE;

    basicPsoDesc.BlendState =
        CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    basicPsoDesc.DepthStencilState =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    basicPsoDesc.SampleMask = UINT_MAX;

    basicPsoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    basicPsoDesc.NumRenderTargets = 1;

    basicPsoDesc.RTVFormats[0] =
        BackBufferFormat;

    basicPsoDesc.SampleDesc.Count = 1;
    basicPsoDesc.SampleDesc.Quality = 0;

    basicPsoDesc.DSVFormat =
        DepthStencilFormat;

    ThrowIfFailed(
        m_dxDevice->CreateGraphicsPipelineState(
            &basicPsoDesc,
            IID_PPV_ARGS(&m_basicPipelineState)
        )
    );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessPsoDesc = {};

	tessPsoDesc.InputLayout =
	{
		tessellationInputLayout.data(),
		static_cast<UINT>(tessellationInputLayout.size())
	};

    tessPsoDesc.pRootSignature = m_rootSignature.Get();

    tessPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(
            m_tessVertexShaderByteCode->GetBufferPointer()
        ),
        m_tessVertexShaderByteCode->GetBufferSize()
    };

    tessPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(
            m_tessPixelShaderByteCode->GetBufferPointer()
        ),
        m_tessPixelShaderByteCode->GetBufferSize()
    };

    tessPsoDesc.HS =
    {
        reinterpret_cast<BYTE*>(
            m_hullShaderByteCode->GetBufferPointer()
        ),
        m_hullShaderByteCode->GetBufferSize()
    };

    tessPsoDesc.DS =
    {
        reinterpret_cast<BYTE*>(
            m_domainShaderByteCode->GetBufferPointer()
        ),
        m_domainShaderByteCode->GetBufferSize()
    };

    tessPsoDesc.RasterizerState =
        CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    tessPsoDesc.RasterizerState.FillMode =
        D3D12_FILL_MODE_WIREFRAME;

    tessPsoDesc.RasterizerState.CullMode =
        D3D12_CULL_MODE_NONE;

    tessPsoDesc.BlendState =
        CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    tessPsoDesc.DepthStencilState =
        CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    tessPsoDesc.SampleMask = UINT_MAX;

    tessPsoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

    tessPsoDesc.NumRenderTargets = 1;

    tessPsoDesc.RTVFormats[0] =
        BackBufferFormat;

    tessPsoDesc.SampleDesc.Count = 1;
    tessPsoDesc.SampleDesc.Quality = 0;

    tessPsoDesc.DSVFormat =
        DepthStencilFormat;

    ThrowIfFailed(
        m_dxDevice->CreateGraphicsPipelineState(
            &tessPsoDesc,
            IID_PPV_ARGS(&m_tessellationPipelineState)
        )
    );
}

void RenderWidget::UpdateObjectConstantBuffer(
	UINT objectIndex,
	const DirectX::XMMATRIX& world)
{
	DirectX::XMMATRIX view =
		m_camera.GetViewMatrix();

	DirectX::XMMATRIX projection =
		m_camera.GetProjectionMatrix();

	DirectX::XMMATRIX worldViewProjection =
		world * view * projection;

	ObjectConstants constants;

	DirectX::XMStoreFloat4x4(
		&constants.World,
		DirectX::XMMatrixTranspose(world)
	);

	DirectX::XMStoreFloat4x4(
		&constants.WorldViewProj,
		DirectX::XMMatrixTranspose(
			worldViewProjection
		)
	);

	constants.CameraPosition =
		m_camera.GetCameraPos();

	BYTE* destination =
		m_mappedData +
		objectIndex *
		m_objectConstantBufferByteSize;

	memcpy(
		destination,
		&constants,
		sizeof(ObjectConstants)
	);
}

void RenderWidget::UpdateWorldViewProjectionBuffer()
{
	m_camera.UpdateViewMatrix();

	// Cube: left side of the scene
	DirectX::XMMATRIX cubeWorld =
		DirectX::XMMatrixScaling(
			0.65f,
			0.65f,
			0.65f
		)
		*
		DirectX::XMMatrixTranslation(
			-1.2f,
			0.5f,
			0.0f
		);

	// Tessellated terrain: right/lower side
	DirectX::XMMATRIX terrainWorld =
		DirectX::XMMatrixScaling(
			1.2f,
			1.2f,
			1.2f
		)
		*
		DirectX::XMMatrixTranslation(
			1.2f,
			-0.7f,
			0.0f
		);

	UpdateObjectConstantBuffer(
		0,
		cubeWorld
	);

	UpdateObjectConstantBuffer(
		1,
		terrainWorld
	);
}

void RenderWidget::Draw()
{
	m_directCmdListAlloc->Reset();

	ResetCommandList(
		m_basicPipelineState.Get()
	);

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto depthStencilViewHandle =
		m_dsvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart();

	m_commandList->ClearRenderTargetView(
		GetCurrentBackBufferView(),
		DirectX::Colors::LightSteelBlue,
		0,
		nullptr
	);

	m_commandList->ClearDepthStencilView(
		depthStencilViewHandle,
		D3D12_CLEAR_FLAG_DEPTH |
		D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr
	);

	// Common state
	ID3D12DescriptorHeap* descriptorHeaps[] =
	{
		m_srvDescriptorHeap.Get()
	};

	m_commandList->SetDescriptorHeaps(
		_countof(descriptorHeaps),
		descriptorHeaps
	);

	m_commandList->SetGraphicsRootSignature(
		m_rootSignature.Get()
	);

	CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(
		m_srvDescriptorHeap
		->GetGPUDescriptorHandleForHeapStart()
	);

	m_commandList->SetGraphicsRootDescriptorTable(
		1,
		textureHandle
	);

	m_commandList->RSSetViewports(
		1,
		&m_screenViewport
	);

	m_commandList->RSSetScissorRects(
		1,
		&m_scissorRect
	);

	m_commandList->OMSetRenderTargets(
		1,
		&GetCurrentBackBufferView(),
		true,
		&depthStencilViewHandle
	);


	// =====================================================
	// CUBE - BASIC PIPELINE
	// =====================================================

	m_commandList->SetPipelineState(
		m_basicPipelineState.Get()
	);

	D3D12_GPU_VIRTUAL_ADDRESS cubeCBAddress =
		m_cbWVProjectionMatrix
		->GetGPUVirtualAddress();

	m_commandList->SetGraphicsRootConstantBufferView(
		0,
		cubeCBAddress
	);

	D3D12_VERTEX_BUFFER_VIEW cubeVBV =
		m_basicMesh.VertexBufferView();

	D3D12_INDEX_BUFFER_VIEW cubeIBV =
		m_basicMesh.IndexBufferView();

	m_commandList->IASetVertexBuffers(
		0,
		1,
		&cubeVBV
	);

	m_commandList->IASetIndexBuffer(
		&cubeIBV
	);

	m_commandList->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	m_commandList->DrawIndexedInstanced(
		m_basicMesh.NumberOfIndices,
		1,
		0,
		0,
		0
	);


	// =====================================================
	// TERRAIN - TESSELLATION PIPELINE
	// =====================================================

	m_commandList->SetPipelineState(
		m_tessellationPipelineState.Get()
	);

	D3D12_GPU_VIRTUAL_ADDRESS terrainCBAddress =
		m_cbWVProjectionMatrix
		->GetGPUVirtualAddress()
		+
		m_objectConstantBufferByteSize;

	m_commandList->SetGraphicsRootConstantBufferView(
		0,
		terrainCBAddress
	);

	D3D12_VERTEX_BUFFER_VIEW terrainVBV =
		m_tessellationMesh.VertexBufferView();

	m_commandList->IASetVertexBuffers(
		0,
		1,
		&terrainVBV
	);

	m_commandList->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST
	);

	m_commandList->DrawInstanced(
		m_tessellationMesh.NumberOfVertices,
		1,
		0,
		0
	);


	// Present
	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	ExecuteCommandList();

	ThrowIfFailed(
		m_swapChain->Present(0, 0)
	);

	m_currBackBuffer =
		(m_currBackBuffer + 1) %
		SwapChainBufferCount;

	FlushCommandQueue();
}
#pragma endregion