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
	
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	UINT64 mCurrentFenceValue = 0; 
	HANDLE mFenceEvent = nullptr;

public:
	CommandQueue() = default;
	~CommandQueue();

	bool Initialize(GraphicsDevice* device);
	void FlushCommandQueue();
	
	ID3D12CommandAllocator* GetCommandAllocator() { return mCommandAllocator.Get(); }
	ID3D12CommandQueue* GetCommandQueue() { return mCommandQueue.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() { return mCommandList.Get(); }
};

