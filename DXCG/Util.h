#pragma once
#include <exception>
#include <stdexcept>
#include <comdef.h>
#include <string>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <wrl.h>

inline void ThrowIfFailed(HRESULT hr);
ComPtr<ID3DBlob> CompileShader(const std::wstring& fileName, const D3D_SHADER_MACRO* defines, const std::string& entryPoint, const std::string& target);

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        _com_error err(hr);
        std::wstring msg = err.ErrorMessage();

        std::string strMsg(msg.begin(), msg.end());
        throw std::runtime_error(strMsg);
    }
}

ComPtr<ID3DBlob> CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entrypoint,
    const std::string& target)
{
    UINT compileFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)  
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = S_OK;

    ComPtr<ID3DBlob> byteCode = nullptr;
    ComPtr<ID3DBlob> errors;
    hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    if (errors != nullptr)
        OutputDebugStringA((char*)errors->GetBufferPointer());

    ThrowIfFailed(hr);

    return byteCode;

}

class MathHelper
{
public:
    static DirectX::XMFLOAT4X4 Identity4x4()
    {
        return { 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f ,0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
    }
};

