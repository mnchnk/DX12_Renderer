#include "CommandQueue.h"
#include "GraphicsDevice.h"
#include "Util.h"

CommandQueue::~CommandQueue()
{
	if (mFenceEvent != nullptr)
	{
		CloseHandle(mFenceEvent);
	}
}

bool CommandQueue::Initialize(GraphicsDevice* device)
{
	ID3D12Device* d3dDevice = device->GetDevice();
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocator)));

	ThrowIfFailed(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&mCommandList)));

	mCommandList->Close();

	ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
	mCurrentFenceValue = 0;
	
	mFenceEvent = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
	if (mFenceEvent == nullptr)
	{
		return false;
	}

	return true;
}

void CommandQueue::FlushCommandQueue()
{
	mCurrentFenceValue++;

	ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFenceValue));

	if (mFence->GetCompletedValue() < mCurrentFenceValue)
	{
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFenceValue, mFenceEvent));
		WaitForSingleObject(mFenceEvent, INFINITE);
	}
}
