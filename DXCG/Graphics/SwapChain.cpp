#include "Graphics/SwapChain.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/CommandQueue.h"
#include "Graphics/Util.h"
#include "Graphics/ShadowMap.h"


bool SwapChain::Initialize(GraphicsDevice* device, CommandQueue* commandQueue, HWND hWnd, UINT width, UINT height)
{
	ID3D12Device* d3dDevice = device->GetDevice();
	IDXGIFactory4* factory = device->GetFactory();
	ID3D12CommandQueue* queue = commandQueue->GetCommandQueue();

	mWidth = width;
	mHeight = height;
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mWidth;
	sd.BufferDesc.Height = mHeight;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = mFrameCount;
	sd.OutputWindow = hWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
	ThrowIfFailed(factory->CreateSwapChain(queue, &sd, &swapChain));
	ThrowIfFailed(swapChain.As(&mSwapChain));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = mFrameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)));

	mRtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < mFrameCount; i++)
	{
		ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i])));
		d3dDevice->CreateRenderTargetView(mRenderTargets[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.ptr += mRtvDescriptorSize;

	}
	mCurrentBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)));

	D3D12_RESOURCE_DESC depthStencilDesc = {};
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mWidth;
	depthStencilDesc.Height = mHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // 24비트는 깊이, 8비트는 스텐실에 사용
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear = {};
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProps.CreationNodeMask = 1;
	heapProps.VisibleNodeMask = 1;

	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 초기 상태는 깊이 쓰기 모드
		&optClear,
		IID_PPV_ARGS(&mDepthStencil)
	));

	d3dDevice->CreateDepthStencilView(mDepthStencil.Get(), nullptr, mDsvHeap->GetCPUDescriptorHandleForHeapStart());

	mCurrentBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();

	return true;
}

void SwapChain::Present()
{
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrentBackBufferIndex = mSwapChain->GetCurrentBackBufferIndex();
}

void SwapChain::OnResize(UINT newWidth, UINT newHeight)
{
}

D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::GetCurrentRtvHandle() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += mCurrentBackBufferIndex * mRtvDescriptorSize;
	return handle;
}



D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::GetCurrentDsvHandle() const
{
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}
