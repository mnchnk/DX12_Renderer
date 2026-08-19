#pragma once
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class ShadowMap
{
public:
	ShadowMap(ID3D12Device* device, UINT width, UINT height);

	D3D12_VIEWPORT GetViewport() const { return mViewport; }
	D3D12_RECT GetScissorRect() const { return mScissorRect; }
	ID3D12Resource* GetResource() const { return mShadowMap.Get(); }

	void BuildDescriptor(
		ID3D12Device* device,
		D3D12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		D3D12_CPU_DESCRIPTOR_HANDLE hCpuDsv
	);

	D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return mGpuSrv; }
	D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const { return mCpuDsv; }

	void OnResize(UINT newWidth, UINT newHeight);

private:
	UINT mWidth = 0;
	UINT mHeight = 0;

	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	ComPtr<ID3D12Resource> mShadowMap = nullptr;
	
	D3D12_CPU_DESCRIPTOR_HANDLE mCpuSrv;
	D3D12_GPU_DESCRIPTOR_HANDLE mGpuSrv;
	D3D12_CPU_DESCRIPTOR_HANDLE mCpuDsv;
};

