#pragma once
#include <exception>
#include <stdexcept>
#include <comdef.h>
#include <string>

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