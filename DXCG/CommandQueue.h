#pragma once
#include "GraphicsDevice.h"
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
class GraphicsDevice;

class CommandQueue
{
private:
	ComPtr<ID3D12CommandQueue> mCommandQueue;
	ComPtr<ID3D12CommandAllocator> mCommandAllocator;
	ComPtr<ID3D12GraphicsCommandList> mCommandList;
	
	ComPtr<ID3D12Fence> mFence;
	UINT64 mCurrentFenceValue = 0; 
	HANDLE mFenceEvent = nullptr;

public:
	CommandQueue() = default;
	~CommandQueue();

	bool Initialize(GraphicsDevice* device);
	void FlushCommandQueue();
	
	ID3D12CommandAllocator* GetCommandAllocator() const { return mCommandAllocator.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return mCommandQueue.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return mCommandList.Get(); }
	ID3D12Fence* GetFence() const { return mFence.Get(); }

	UINT mCurrFence = 0;
};

