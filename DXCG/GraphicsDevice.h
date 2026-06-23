#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class GraphicsDevice
{
private:
	ComPtr<ID3D12Device> mD3D12Device;
	ComPtr<IDXGIFactory4> mDxgiFactory;

	void EnableDebugLayer();
public:
	GraphicsDevice() = default;
	~GraphicsDevice() = default;

	bool Initialize();

	ID3D12Device* GetDevice() { return mD3D12Device.Get(); }
	IDXGIFactory4* GetFactory() { return mDxgiFactory.Get(); }
};

