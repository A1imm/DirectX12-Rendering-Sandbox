#ifndef _RENDERHELPERCLASS_H
#define _RENDERHELPERCLASS_H

#include "common.h"

namespace Geometry
{
	struct Vertex
	{
		Vertex(DirectX::XMFLOAT3 position, DirectX::XMFLOAT2 uv) :Position(position), UV(uv) {}
		DirectX::XMFLOAT3 Position;
		DirectX::XMFLOAT2 UV;
	};
	using VertexBuffer = std::vector<Vertex>;

	inline VertexBuffer CreateQuadPatchGeometry()
	{
		VertexBuffer vertices;
		vertices.emplace_back(DirectX::XMFLOAT3(-1.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0, 1.0));
		vertices.emplace_back(DirectX::XMFLOAT3(-1.0f, 0.0f, 1.0f), DirectX::XMFLOAT2(1.0, 1.0));
		vertices.emplace_back(DirectX::XMFLOAT3(1.0f, 0.0f, 1.0f), DirectX::XMFLOAT2(1.0, 0.0));
		vertices.emplace_back(DirectX::XMFLOAT3(1.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0, 0.0));	

		return std::move(vertices);
	}

	inline DirectX::XMFLOAT4X4 Identity4x4()
	{
		static DirectX::XMFLOAT4X4 I(
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);

		return I;
	}

	struct Camera
	{
		float Theta = 1.5f * DirectX::XM_PI;
		float Phi = DirectX::XM_PIDIV4;
		float Radius = 5.0f;
		float Width = 0.0f;
		float Height = 0.0f;

		DirectX::XMMATRIX GetViewMatrix() const
		{
			return DirectX::XMLoadFloat4x4(&m_view);
		}

		DirectX::XMMATRIX GetProjectionMatrix() const
		{
			return DirectX::XMLoadFloat4x4(&m_proj);
		}

		void UpdateViewMatrix()
		{
			float x = Radius * sinf(Phi) * cosf(Theta);
			float z = Radius * sinf(Phi) * sinf(Theta);
			float y = Radius * cosf(Phi);

			// Build the view matrix.
			DirectX::XMVECTOR pos = DirectX::XMVectorSet(x, y, z, 1.0f);
			DirectX::XMVECTOR target = DirectX::XMVectorZero();
			DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
			DirectX::XMStoreFloat4x4(&m_view, view);
		}

		DirectX::XMFLOAT3 GetCameraPos() const
		{
			float x = Radius * sinf(Phi) * cosf(Theta);
			float z = Radius * sinf(Phi) * sinf(Theta);
			float y = Radius * cosf(Phi);
			return DirectX::XMFLOAT3(x, y, z);
		}

		void UpdateProjetionMatrix()
		{
			const auto aspectRation = Width / Height;
			DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(0.25f * static_cast<float>(M_PI), aspectRation, 1.0f, 1000.0f);
			DirectX::XMStoreFloat4x4(&m_proj, proj);
		}
	private:
		DirectX::XMFLOAT4X4 m_view = Geometry::Identity4x4();
		DirectX::XMFLOAT4X4 m_proj = Geometry::Identity4x4();
	};
};

namespace DirectXHelper
{
	Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target) noexcept;

	Microsoft::WRL::ComPtr< IWICImagingFactory2> CreateWICFactory() noexcept;

	//Function loads a file image to memory buffer
	//Returns: A tuple containing pointer to the buffer, image width and image height
	std::tuple<std::unique_ptr<BYTE[]> /*buffer*/, UINT /*width*/, UINT /*hegiht*/> LoadTextureToBuffer(const wchar_t* path) noexcept;

	inline UINT CalcConstantBufferByteSize(UINT byteSize) noexcept
	{
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}
};
#endif