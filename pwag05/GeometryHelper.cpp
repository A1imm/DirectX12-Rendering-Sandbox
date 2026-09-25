#include "GeometryHelper.h"

Microsoft::WRL::ComPtr<ID3DBlob> DirectXHelper::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target) noexcept
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	HRESULT results = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

	if (errors != nullptr)
	{
		OutputDebugStringA((char*)errors->GetBufferPointer());
	}

	assert(SUCCEEDED(results));

	return byteCode;
}

Microsoft::WRL::ComPtr< IWICImagingFactory2> DirectXHelper::CreateWICFactory() noexcept
{
	Microsoft::WRL::ComPtr< IWICImagingFactory2> wicFactory;
	if (wicFactory != nullptr)
	{
		return wicFactory;
	}
	const HRESULT result = CoCreateInstance(CLSID_WICImagingFactory2, //CLS ID of the object we're making
		nullptr,													  //not part of an agregate
		CLSCTX_INPROC_SERVER,									      //DLL runs in the same process
		__uuidof(IWICImagingFactory2),                                //Ref to interface process
		reinterpret_cast<LPVOID*>(wicFactory.GetAddressOf()));                 //The pointer that will contain our factory object


	assert(SUCCEEDED(result) && "Can't create Windows Imaging Component factory object");
	return wicFactory;
}

//Function loads a file image to memory buffer
//Returns: A tuple containing pointer to the buffer, image width and image height
std::tuple<std::unique_ptr<BYTE[]>, UINT, UINT> DirectXHelper::LoadTextureToBuffer(const wchar_t* path) noexcept
{
	assert(std::filesystem::exists(path) && "Invalid path");

	using namespace Microsoft::WRL;
	auto wicFactory = CreateWICFactory();

	ComPtr<IWICBitmapDecoder> imageDecoder;
	HRESULT result = wicFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, imageDecoder.GetAddressOf());
	if (FAILED(result))
	{
		//Can't create the decoder. Is the path valid?
		return { nullptr, 0, 0 };
	}

	//Validate the image
	unsigned frameCount = 0;
	result = imageDecoder->GetFrameCount(&frameCount);
	if (FAILED(result) || frameCount == 0)
	{
		assert(false && "Invalid image file");
		return { nullptr, 0, 0 };
	}

	ComPtr<IWICBitmapFrameDecode> frameDecoder;
	constexpr UINT frameNumber = 0;
	result = imageDecoder->GetFrame(frameNumber, frameDecoder.GetAddressOf());
	assert(SUCCEEDED(result));

	WICPixelFormatGUID pixelFormat;
	result = frameDecoder->GetPixelFormat(&pixelFormat);
	assert(SUCCEEDED(result));

	UINT width, height;
	result = frameDecoder->GetSize(&width, &height);
	assert(SUCCEEDED(result));

	ComPtr<IWICFormatConverter> frameConverter;
	result = wicFactory->CreateFormatConverter(frameConverter.GetAddressOf());
	assert(SUCCEEDED(result));

	result = frameConverter->Initialize(frameDecoder.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
		nullptr, 0.0f, WICBitmapPaletteTypeCustom);
	assert(SUCCEEDED(result));

	BOOL canConvert;
	result = frameConverter->CanConvert(pixelFormat, GUID_WICPixelFormat32bppRGBA, &canConvert);
	if (FAILED(result) || canConvert == false)
	{
		assert(false && "Can't covert to a supported format");
		return { nullptr, 0, 0 };
	}

	constexpr unsigned int BytesPerPixel = 32; //GUID_WICPixelFormat32bppRGBA is 32bpp!
	const unsigned int rowBytes = width * BytesPerPixel;
	const unsigned int byteSize = height * rowBytes;

	std::unique_ptr<BYTE[]> convertedImage(new(std::nothrow) BYTE[byteSize]);
	if (convertedImage == nullptr)
	{
		assert(false && "Not enough memory");
		return { nullptr, 0, 0 };
	}
	
	result = frameConverter->CopyPixels(nullptr, rowBytes, byteSize, convertedImage.get());
	assert(SUCCEEDED(result) && "Can't copy pixels to the image buffer");
	return { std::move(convertedImage), width, height };
}