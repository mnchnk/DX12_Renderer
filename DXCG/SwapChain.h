#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class SwapChain
{
private:
	ComPtr<IDXGISwapChain4> mSwapChain;

public:
	SwapChain() = default;
	~SwapChain() = default;

	bool Initialize();
};

