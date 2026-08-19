#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class GraphicsDevice;
class CommandQueue;

using Microsoft::WRL::ComPtr;

class SwapChain
{
private:
	static const int mFrameCount = 2;

	UINT mWidth = 0;
	UINT mHeight = 0;

	ComPtr<IDXGISwapChain4> mSwapChain;

	ComPtr<ID3D12Resource> mRenderTargets[mFrameCount];
	ComPtr<ID3D12Resource> mDepthStencil;
	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mDsvHeap;


	UINT mCurrentBackBufferIndex = 0;
	UINT mRtvDescriptorSize;
public:
	SwapChain() = default;
	~SwapChain() = default;

	bool Initialize(GraphicsDevice* device, CommandQueue* commandQueue, HWND hWnd, UINT width, UINT height);

	void Present();
	void OnResize(UINT newWidth, UINT newHeight);

	UINT getWidth() const { return mWidth; }
	UINT getHeight() const { return mHeight; }
	ID3D12Resource* GetCurrentRenderTarget() const { return mRenderTargets[mCurrentBackBufferIndex].Get(); }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtvHandle() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDsvHandle() const;
};

