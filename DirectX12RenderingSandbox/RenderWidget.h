#ifndef _RENDERWIDGETCLASS_H
#define _RENDERWIDGETCLASS_H

#include "GeometryHelper.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = Geometry::Identity4x4();
    DirectX::XMFLOAT4X4 WorldViewProj = Geometry::Identity4x4();
    DirectX::XMFLOAT3 CameraPosition = { 0.0f, 0.0f, 0.0f };
    float Padding = 0.0f;
};

class RenderWidget
{
    
public:
	explicit RenderWidget(unsigned int width, unsigned int height, HWND hWnd);
    void UpdateWorldViewProjectionBuffer();
    void Initialize();
    void Draw();
    void Resize(int width, int height);
    Geometry::Camera& GetCamera() {
        return m_camera;
    }
    
private:
    UINT m_rtvDescriptorSize = 0;
	//DirectX12 objects
    void CreateDXDeviceAndFactory();
    void CreateSwapChain(unsigned int width, unsigned int height);
    void ResizeSwapChain(unsigned int width, unsigned int height);
    ID3D12Resource* RenderWidget::GetCurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE RenderWidget::GetCurrentBackBufferView()const;
    const static DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static const int SwapChainBufferCount = 2;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_dxDevice;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[SwapChainBufferCount];
    HWND m_hWnd;
    int m_width, m_height;
    unsigned int m_currBackBuffer = 0;


    //Control objects
    void CreateCommandObjects();
    void FlushCommandQueue();
    void ExecuteCommandList();
    void ResetCommandList(ID3D12PipelineState* pipelineState = nullptr);
    UINT64 m_currentFence = 0;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_directCmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    //Descriptor Heaps
    void CreateDescriptorHeaps();
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvDescriptorHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvDescriptorHeap;

    // Render Target View and Depth Stencil View
    void CreateRenderTargetView();
    void CreateDepthStencilView(unsigned int width, unsigned int height);
    const static DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_depthStencilBuffer;

    //Constant buffers
    void CreateWorldViewProjectionMatrixBuffer();
    void UpdateObjectConstantBuffer(
        UINT objectIndex,
        const DirectX::XMMATRIX& world
    );
    BYTE* m_mappedData = nullptr;
    UINT m_objectConstantBufferByteSize = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_cbWVProjectionMatrix;

    //Shaders
    void CompileShaders();

    // Basic pipeline
    Microsoft::WRL::ComPtr<ID3DBlob> m_basicVertexShaderByteCode;
    Microsoft::WRL::ComPtr<ID3DBlob> m_basicPixelShaderByteCode;

    // Tessellation pipeline
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessVertexShaderByteCode;
    Microsoft::WRL::ComPtr<ID3DBlob> m_tessPixelShaderByteCode;
    Microsoft::WRL::ComPtr<ID3DBlob> m_hullShaderByteCode;
    Microsoft::WRL::ComPtr<ID3DBlob> m_domainShaderByteCode;

    // Pipeline states and Root signature
    void BuildRootSignature();
    void CreateGraphicPipelines();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_basicPipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_tessellationPipelineState;


    //Textures
    void LoadTexture(const wchar_t* file);
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureResource = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_textureResourceUpload = nullptr;


    //Viewport
    void UpdateViewport(unsigned int width, unsigned int height);
    D3D12_VIEWPORT m_screenViewport;
    D3D12_RECT m_scissorRect;


    //Geometry, Camera, Vertex and Index buffer
    struct MeshBuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;

        Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

        UINT VertexByteStride = 0;
        UINT VertexBufferByteSize = 0;
        size_t NumberOfVertices = 0;

        UINT IndexBufferByteSize = 0;
        UINT NumberOfIndices = 0;

        D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
        {
            D3D12_VERTEX_BUFFER_VIEW view;

            view.BufferLocation =
                VertexBufferGPU->GetGPUVirtualAddress();

            view.StrideInBytes =
                VertexByteStride;

            view.SizeInBytes =
                VertexBufferByteSize;

            return view;
        }

        D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
        {
            D3D12_INDEX_BUFFER_VIEW view;

            view.BufferLocation =
                IndexBufferGPU->GetGPUVirtualAddress();

            view.Format =
                DXGI_FORMAT_R16_UINT;

            view.SizeInBytes =
                IndexBufferByteSize;

            return view;
        }
    };
    void LoadGeometry();
    void LoadVertexBuffer(
        const Geometry::VertexBuffer& vertices,
        MeshBuffer& mesh
    );

    void LoadIndexBuffer(
        const Geometry::IndexBuffer& indices,
        MeshBuffer& mesh
    );
    Geometry::Camera m_camera;

    MeshBuffer m_basicMesh;
    MeshBuffer m_tessellationMesh;
};

#endif
