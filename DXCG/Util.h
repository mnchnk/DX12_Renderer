#pragma once
#include <exception>
#include <stdexcept>
#include <comdef.h>
#include <string>
#include <DirectXMath.h>

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