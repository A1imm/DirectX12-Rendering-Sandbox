#ifndef _COMMON_H
#define _COMMON_H

#include <iostream>
#include <cassert>
#include <sstream>
#include <fstream>
#include <vector>
#include <list>
#include <string>
#include <windows.h>
#include <tuple>
#include <filesystem>

// math
#define _USE_MATH_DEFINES
#include <math.h>
#define NOMINMAX

//dx
#include <d3dcompiler.h>
#include <d3d12.h>
#include "d3dx12.h"

#include "DDSTextureLoader.h"
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include <wrl.h> //windows runtime template library

#include <wincodec.h>
#include <wincodecsdk.h>
#include <unordered_map>
#include <map>
#include <shlwapi.h>


#pragma region ErrorHandling
class DxException
{
public:
    DxException() = default;
    DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber):
        ErrorCode(hr),
        FunctionName(functionName),
        Filename(filename),
        LineNumber(lineNumber)
    {}

    HRESULT ErrorCode = S_OK;
    std::wstring FunctionName;
    std::wstring Filename;
    int LineNumber = -1;
};

inline std::wstring AnsiToWString(const std::string& str)
{
    WCHAR buffer[512];
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
    return std::wstring(buffer);
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif
#pragma endregion

#endif
