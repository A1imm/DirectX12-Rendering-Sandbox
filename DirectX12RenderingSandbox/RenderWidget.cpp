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
	CreateShadowMap();

	//2. Create shaders and resources
	CreateWorldViewProjectionMatrixBuffer();
	CreateSceneConstantBuffer();
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

	LoadTexture(
		L"WoodCrateNormal.png",
		m_cubeNormalMap,
		3
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
	UpdateSceneConstantBuffer();
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
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	result = m_dxDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(m_dsvDescriptorHeap.GetAddressOf()));
	assert(SUCCEEDED(result) && "Can't create the depth stencil view heap descriptor");
	ThrowIfFailed(result);
	m_dsvDescriptorSize =
		m_dxDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV
		);

	// Shader Resource View heap descriptor
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 5;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	result = m_dxDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvDescriptorHeap));
	assert(SUCCEEDED(result) && "Can't create the shader resource view heap descriptor");
	ThrowIfFailed(result);
	m_srvDescriptorSize =
		m_dxDevice->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		);
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

void RenderWidget::CreateShadowMap()
{
	D3D12_RESOURCE_DESC shadowDesc = {};

	shadowDesc.Dimension =
		D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	shadowDesc.Alignment = 0;

	shadowDesc.Width =
		ShadowMapSize;

	shadowDesc.Height =
		ShadowMapSize;

	shadowDesc.DepthOrArraySize = 1;
	shadowDesc.MipLevels = 1;

	shadowDesc.Format =
		ShadowMapResourceFormat;

	shadowDesc.SampleDesc.Count = 1;
	shadowDesc.SampleDesc.Quality = 0;

	shadowDesc.Layout =
		D3D12_TEXTURE_LAYOUT_UNKNOWN;

	shadowDesc.Flags =
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


	D3D12_CLEAR_VALUE clearValue = {};

	clearValue.Format =
		ShadowMapDSVFormat;

	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;


	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(
				D3D12_HEAP_TYPE_DEFAULT
			),
			D3D12_HEAP_FLAG_NONE,
			&shadowDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			&clearValue,
			IID_PPV_ARGS(&m_shadowMap)
		)
	);

	m_shadowMap->SetName(
		L"Shadow Map"
	);


	// =========================================
	// DSV - descriptor slot 1
	// =========================================

	CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(
		m_dsvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart(),
		1,
		m_dsvDescriptorSize
	);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};

	dsvDesc.Flags =
		D3D12_DSV_FLAG_NONE;

	dsvDesc.ViewDimension =
		D3D12_DSV_DIMENSION_TEXTURE2D;

	dsvDesc.Format =
		ShadowMapDSVFormat;

	dsvDesc.Texture2D.MipSlice = 0;

	m_dxDevice->CreateDepthStencilView(
		m_shadowMap.Get(),
		&dsvDesc,
		shadowDsvHandle
	);


	// =========================================
	// SRV - descriptor slot 4 / register t4
	// =========================================

	CD3DX12_CPU_DESCRIPTOR_HANDLE shadowSrvHandle(
		m_srvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart(),
		4,
		m_srvDescriptorSize
	);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

	srvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.Format =
		ShadowMapSRVFormat;

	srvDesc.ViewDimension =
		D3D12_SRV_DIMENSION_TEXTURE2D;

	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	m_dxDevice->CreateShaderResourceView(
		m_shadowMap.Get(),
		&srvDesc,
		shadowSrvHandle
	);


	// =========================================
	// Shadow viewport
	// =========================================

	m_shadowViewport.TopLeftX = 0.0f;
	m_shadowViewport.TopLeftY = 0.0f;

	m_shadowViewport.Width =
		static_cast<float>(ShadowMapSize);

	m_shadowViewport.Height =
		static_cast<float>(ShadowMapSize);

	m_shadowViewport.MinDepth = 0.0f;
	m_shadowViewport.MaxDepth = 1.0f;


	m_shadowScissorRect =
	{
		0,
		0,
		static_cast<LONG>(ShadowMapSize),
		static_cast<LONG>(ShadowMapSize)
	};
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

	constexpr UINT objectCount = 3;

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

void RenderWidget::CreateSceneConstantBuffer()
{
	m_sceneConstantBufferByteSize =
		DirectXHelper::CalcConstantBufferByteSize(
			sizeof(SceneConstants)
		);

	ThrowIfFailed(
		m_dxDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(
				D3D12_HEAP_TYPE_UPLOAD
			),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(
				m_sceneConstantBufferByteSize
			),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(
				&m_cbScene
			)
		)
	);

	ThrowIfFailed(
		m_cbScene->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(
				&m_sceneMappedData
				)
		)
	);
}

void RenderWidget::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE resourceTable;
	resourceTable.Init(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		5,
		0
	);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// b0 - per-object data
	slotRootParameter[0].InitAsConstantBufferView(0);

	// t0-t2 - textures
	slotRootParameter[1].InitAsDescriptorTable(
		1,
		&resourceTable
	);

	// b1 - scene / lighting data
	slotRootParameter[2].InitAsConstantBufferView(1);

	CD3DX12_STATIC_SAMPLER_DESC samplers[2];

	// Normal texture sampler - s0
	samplers[0] =
		CD3DX12_STATIC_SAMPLER_DESC(
			0,
			D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
			D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
			D3D12_TEXTURE_ADDRESS_MODE_MIRROR
		);

	// Shadow comparison sampler - s1
	samplers[1] =
		CD3DX12_STATIC_SAMPLER_DESC(
			1,
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,
			0.0f,
			16,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
		);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		3,
		slotRootParameter,
		2,
		samplers,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

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

	// =====================================================
	// Shadow shaders
	// =====================================================

	m_shadowBasicVertexShaderByteCode =
		DirectXHelper::CompileShader(
			L"Shadow.hlsl",
			nullptr,
			"VS_BasicShadow",
			"vs_5_0"
		);

	assert(
		m_shadowBasicVertexShaderByteCode
	);


	m_shadowTerrainVertexShaderByteCode =
		DirectXHelper::CompileShader(
			L"Shadow.hlsl",
			nullptr,
			"VS_TerrainShadow",
			"vs_5_0"
		);

	assert(
		m_shadowTerrainVertexShaderByteCode
	);


	m_shadowTerrainHullShaderByteCode =
		DirectXHelper::CompileShader(
			L"Shadow.hlsl",
			nullptr,
			"HS_TerrainShadow",
			"hs_5_0"
		);

	assert(
		m_shadowTerrainHullShaderByteCode
	);


	m_shadowTerrainDomainShaderByteCode =
		DirectXHelper::CompileShader(
			L"Shadow.hlsl",
			nullptr,
			"DS_TerrainShadow",
			"ds_5_0"
		);

	assert(
		m_shadowTerrainDomainShaderByteCode
	);

	// =====================================================
	// Billboard shaders
	// =====================================================

	m_billboardVertexShaderByteCode =
		DirectXHelper::CompileShader(
			L"Billboard.hlsl",
			nullptr,
			"VS_Main",
			"vs_5_0"
		);

	assert(
		m_billboardVertexShaderByteCode
	);


	m_billboardGeometryShaderByteCode =
		DirectXHelper::CompileShader(
			L"Billboard.hlsl",
			nullptr,
			"GS_Main",
			"gs_5_0"
		);

	assert(
		m_billboardGeometryShaderByteCode
	);


	m_billboardPixelShaderByteCode =
		DirectXHelper::CompileShader(
			L"Billboard.hlsl",
			nullptr,
			"PS_Main",
			"ps_5_0"
		);

	assert(
		m_billboardPixelShaderByteCode
	);
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

	// Billboard point cloud
	const auto billboardPoints =
		Geometry::CreateBillboardPoints();

	LoadVertexBuffer(
		billboardPoints,
		m_billboardMesh
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
		},
		{
			"TANGENT",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			32,
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
		D3D12_FILL_MODE_SOLID;

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

	// =====================================================
	// SHADOW PIPELINE - CUBE
	// =====================================================

	std::vector<D3D12_INPUT_ELEMENT_DESC>
		shadowBasicInputLayout =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};


	D3D12_GRAPHICS_PIPELINE_STATE_DESC
		shadowBasicPsoDesc = {};

	shadowBasicPsoDesc.InputLayout =
	{
		shadowBasicInputLayout.data(),
		static_cast<UINT>(
			shadowBasicInputLayout.size()
		)
	};

	shadowBasicPsoDesc.pRootSignature =
		m_rootSignature.Get();

	shadowBasicPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(
			m_shadowBasicVertexShaderByteCode
				->GetBufferPointer()
		),
		m_shadowBasicVertexShaderByteCode
			->GetBufferSize()
	};


	// No pixel shader.
	// Shadow pass writes depth only.

	shadowBasicPsoDesc.RasterizerState =
		CD3DX12_RASTERIZER_DESC(
			D3D12_DEFAULT
		);

	shadowBasicPsoDesc.RasterizerState.CullMode =
		D3D12_CULL_MODE_NONE;


	// Bias helps prevent shadow acne.
	shadowBasicPsoDesc.RasterizerState.DepthBias =
		1000;

	shadowBasicPsoDesc.RasterizerState
		.SlopeScaledDepthBias =
		1.0f;

	shadowBasicPsoDesc.RasterizerState
		.DepthBiasClamp =
		0.0f;


	shadowBasicPsoDesc.BlendState =
		CD3DX12_BLEND_DESC(
			D3D12_DEFAULT
		);

	shadowBasicPsoDesc.DepthStencilState =
		CD3DX12_DEPTH_STENCIL_DESC(
			D3D12_DEFAULT
		);

	shadowBasicPsoDesc.SampleMask =
		UINT_MAX;

	shadowBasicPsoDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;


	// Depth-only pass.
	shadowBasicPsoDesc.NumRenderTargets = 0;

	shadowBasicPsoDesc.DSVFormat =
		ShadowMapDSVFormat;

	shadowBasicPsoDesc.SampleDesc.Count = 1;
	shadowBasicPsoDesc.SampleDesc.Quality = 0;


	ThrowIfFailed(
		m_dxDevice->CreateGraphicsPipelineState(
			&shadowBasicPsoDesc,
			IID_PPV_ARGS(
				&m_shadowBasicPipelineState
			)
		)
	);

	// =====================================================
	// SHADOW PIPELINE - TESSELLATED TERRAIN
	// =====================================================

	std::vector<D3D12_INPUT_ELEMENT_DESC>
		shadowTerrainInputLayout =
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


	D3D12_GRAPHICS_PIPELINE_STATE_DESC
		shadowTerrainPsoDesc = {};

	shadowTerrainPsoDesc.InputLayout =
	{
		shadowTerrainInputLayout.data(),
		static_cast<UINT>(
			shadowTerrainInputLayout.size()
		)
	};

	shadowTerrainPsoDesc.pRootSignature =
		m_rootSignature.Get();


	shadowTerrainPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(
			m_shadowTerrainVertexShaderByteCode
				->GetBufferPointer()
		),
		m_shadowTerrainVertexShaderByteCode
			->GetBufferSize()
	};

	shadowTerrainPsoDesc.HS =
	{
		reinterpret_cast<BYTE*>(
			m_shadowTerrainHullShaderByteCode
				->GetBufferPointer()
		),
		m_shadowTerrainHullShaderByteCode
			->GetBufferSize()
	};

	shadowTerrainPsoDesc.DS =
	{
		reinterpret_cast<BYTE*>(
			m_shadowTerrainDomainShaderByteCode
				->GetBufferPointer()
		),
		m_shadowTerrainDomainShaderByteCode
			->GetBufferSize()
	};


	shadowTerrainPsoDesc.RasterizerState =
		CD3DX12_RASTERIZER_DESC(
			D3D12_DEFAULT
		);

	shadowTerrainPsoDesc.RasterizerState.CullMode =
		D3D12_CULL_MODE_NONE;

	shadowTerrainPsoDesc.RasterizerState.DepthBias =
		1000;

	shadowTerrainPsoDesc.RasterizerState
		.SlopeScaledDepthBias =
		1.0f;

	shadowTerrainPsoDesc.RasterizerState
		.DepthBiasClamp =
		0.0f;


	shadowTerrainPsoDesc.BlendState =
		CD3DX12_BLEND_DESC(
			D3D12_DEFAULT
		);

	shadowTerrainPsoDesc.DepthStencilState =
		CD3DX12_DEPTH_STENCIL_DESC(
			D3D12_DEFAULT
		);

	shadowTerrainPsoDesc.SampleMask =
		UINT_MAX;

	shadowTerrainPsoDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	shadowTerrainPsoDesc.NumRenderTargets = 0;

	shadowTerrainPsoDesc.DSVFormat =
		ShadowMapDSVFormat;

	shadowTerrainPsoDesc.SampleDesc.Count = 1;
	shadowTerrainPsoDesc.SampleDesc.Quality = 0;


	ThrowIfFailed(
		m_dxDevice->CreateGraphicsPipelineState(
			&shadowTerrainPsoDesc,
			IID_PPV_ARGS(
				&m_shadowTerrainPipelineState
			)
		)
	);

	// =====================================================
	// BILLBOARD PIPELINE
	// Point -> Geometry Shader -> camera-facing quad
	// =====================================================

	std::vector<D3D12_INPUT_ELEMENT_DESC>
		billboardInputLayout =
	{
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};


	D3D12_GRAPHICS_PIPELINE_STATE_DESC
		billboardPsoDesc = {};


	billboardPsoDesc.InputLayout =
	{
		billboardInputLayout.data(),
		static_cast<UINT>(
			billboardInputLayout.size()
		)
	};


	billboardPsoDesc.pRootSignature =
		m_rootSignature.Get();


	billboardPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(
			m_billboardVertexShaderByteCode
				->GetBufferPointer()
		),
		m_billboardVertexShaderByteCode
			->GetBufferSize()
	};


	billboardPsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(
			m_billboardGeometryShaderByteCode
				->GetBufferPointer()
		),
		m_billboardGeometryShaderByteCode
			->GetBufferSize()
	};


	billboardPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(
			m_billboardPixelShaderByteCode
				->GetBufferPointer()
		),
		m_billboardPixelShaderByteCode
			->GetBufferSize()
	};


	billboardPsoDesc.RasterizerState =
		CD3DX12_RASTERIZER_DESC(
			D3D12_DEFAULT
		);

	billboardPsoDesc.RasterizerState.CullMode =
		D3D12_CULL_MODE_NONE;


	billboardPsoDesc.BlendState =
		CD3DX12_BLEND_DESC(
			D3D12_DEFAULT
		);


	billboardPsoDesc.DepthStencilState =
		CD3DX12_DEPTH_STENCIL_DESC(
			D3D12_DEFAULT
		);


	billboardPsoDesc.SampleMask =
		UINT_MAX;


	billboardPsoDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;


	billboardPsoDesc.NumRenderTargets = 1;

	billboardPsoDesc.RTVFormats[0] =
		BackBufferFormat;


	billboardPsoDesc.DSVFormat =
		DepthStencilFormat;


	billboardPsoDesc.SampleDesc.Count = 1;
	billboardPsoDesc.SampleDesc.Quality = 0;


	ThrowIfFailed(
		m_dxDevice->CreateGraphicsPipelineState(
			&billboardPsoDesc,
			IID_PPV_ARGS(
				&m_billboardPipelineState
			)
		)
	);
}

// =====================================================
// LIGHT VIEW / PROJECTION
// =====================================================

DirectX::XMMATRIX
RenderWidget::GetLightViewProjectionMatrix() const
{
	DirectX::XMVECTOR lightDirection =
		DirectX::XMLoadFloat3(
			&m_sceneConstants.LightDirection
		);

	lightDirection =
		DirectX::XMVector3Normalize(
			lightDirection
		);

	DirectX::XMVECTOR lightPosition =
		DirectX::XMVectorScale(
			lightDirection,
			-10.0f
		);

	DirectX::XMVECTOR target =
		DirectX::XMVectorZero();

	DirectX::XMVECTOR up =
		DirectX::XMVectorSet(
			0.0f,
			1.0f,
			0.0f,
			0.0f
		);

	DirectX::XMMATRIX lightView =
		DirectX::XMMatrixLookAtLH(
			lightPosition,
			target,
			up
		);

	DirectX::XMMATRIX lightProjection =
		DirectX::XMMatrixOrthographicLH(
			10.0f,
			10.0f,
			1.0f,
			30.0f
		);

	return lightView * lightProjection;
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

	DirectX::XMMATRIX lightViewProjection =
		GetLightViewProjectionMatrix();

	DirectX::XMMATRIX worldLightViewProjection =
		world *	lightViewProjection;

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

	DirectX::XMStoreFloat4x4(
		&constants.WorldLightViewProj,
		DirectX::XMMatrixTranspose(
			worldLightViewProjection
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

void RenderWidget::UpdateSceneConstantBuffer()
{
	memcpy(
		m_sceneMappedData,
		&m_sceneConstants,
		sizeof(SceneConstants)
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

	// Billboard point positions are currently stored
	// directly in world space.
	DirectX::XMMATRIX billboardWorld =
		DirectX::XMMatrixIdentity();

	UpdateObjectConstantBuffer(
		2,
		billboardWorld
	);
}

void RenderWidget::RenderShadowPass()
{
	// =====================================================
	// Prepare shadow map for depth writing
	// =====================================================

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_shadowMap.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		)
	);


	CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle(
		m_dsvDescriptorHeap
		->GetCPUDescriptorHandleForHeapStart(),
		1,
		m_dsvDescriptorSize
	);


	m_commandList->ClearDepthStencilView(
		shadowDsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);


	m_commandList->RSSetViewports(
		1,
		&m_shadowViewport
	);

	m_commandList->RSSetScissorRects(
		1,
		&m_shadowScissorRect
	);


	// No color render target.
	m_commandList->OMSetRenderTargets(
		0,
		nullptr,
		false,
		&shadowDsvHandle
	);


	m_commandList->SetGraphicsRootSignature(
		m_rootSignature.Get()
	);


	// Terrain shadow shader needs the height map at t1.
	ID3D12DescriptorHeap* descriptorHeaps[] =
	{
		m_srvDescriptorHeap.Get()
	};

	m_commandList->SetDescriptorHeaps(
		_countof(descriptorHeaps),
		descriptorHeaps
	);


	CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(
		m_srvDescriptorHeap
		->GetGPUDescriptorHandleForHeapStart()
	);

	m_commandList->SetGraphicsRootDescriptorTable(
		1,
		textureHandle
	);


	// =====================================================
	// CUBE -> SHADOW MAP
	// =====================================================

	m_commandList->SetPipelineState(
		m_shadowBasicPipelineState.Get()
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
	// TERRAIN -> SHADOW MAP
	// =====================================================

	m_commandList->SetPipelineState(
		m_shadowTerrainPipelineState.Get()
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

	// =====================================================
	// BILLBOARDS - GEOMETRY SHADER PIPELINE
	// =====================================================

	m_commandList->SetPipelineState(
		m_billboardPipelineState.Get()
	);


	D3D12_GPU_VIRTUAL_ADDRESS billboardCBAddress =
		m_cbWVProjectionMatrix
		->GetGPUVirtualAddress()
		+
		2 * m_objectConstantBufferByteSize;


	m_commandList->SetGraphicsRootConstantBufferView(
		0,
		billboardCBAddress
	);


	D3D12_VERTEX_BUFFER_VIEW billboardVBV =
		m_billboardMesh.VertexBufferView();


	m_commandList->IASetVertexBuffers(
		0,
		1,
		&billboardVBV
	);


	m_commandList->IASetIndexBuffer(
		nullptr
	);


	m_commandList->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_POINTLIST
	);


	m_commandList->DrawInstanced(
		static_cast<UINT>(
			m_billboardMesh.NumberOfVertices
			),
		1,
		0,
		0
	);

	// =====================================================
	// Shadow map becomes readable by main pass
	// =====================================================

	m_commandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			m_shadowMap.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_GENERIC_READ
		)
	);
}

void RenderWidget::Draw()
{
	m_directCmdListAlloc->Reset();

	ResetCommandList(
		m_shadowBasicPipelineState.Get()
	);

	// First pass:
	// render scene depth from light's perspective.
	RenderShadowPass();

	// Second pass:
	// normal camera rendering.
	m_commandList->SetPipelineState(
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

	m_commandList->SetGraphicsRootConstantBufferView(
		2,
		m_cbScene->GetGPUVirtualAddress()
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