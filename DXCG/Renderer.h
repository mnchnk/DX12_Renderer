#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <d3d12.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"
#include "FrameResource.h"

enum class RenderItemType
{
	Opaque = 0

};

struct RenderItem
{
	DirectX::XMFLOAT4X4 World;
	DirectX::XMFLOAT4X4 TexTransform;

	UINT ObjectCBIndex;
	UINT8 NumFramesDirty = 3;
};

class Renderer
{
private:
	//Graphics Basic 
	std::unique_ptr<GraphicsDevice> mGraphicsDevice;
	std::unique_ptr<CommandQueue> mCommandQueue;
	std::unique_ptr<SwapChain> mSwapChain;

	std::unique_ptr<ID3D12RootSignature> mRootSignature;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;	
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
	
	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;

	UINT mClientWidth = 800;
	UINT mClientHeight = 600;

	HWND mHWnd;

	//FrameResource
	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	UINT8 mCurrFrameResourceIndex;
	FrameResource* mCurrFrameResource = nullptr;

	//RenderItem
	std::unordered_map<RenderItemType, std::vector<RenderItem*>> mRenderItemsByType;
	std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;

public:
	Renderer(HWND hWnd, UINT clientWidth, UINT clientHeight) :mHWnd(hWnd), mClientWidth(clientWidth), mClientHeight(clientHeight) {}
	~Renderer() = default;

	bool Initialize();
	bool InitializeFrameResource();
	bool InitializeRootSignature();
	bool InitializeShadersAndInputLayout();
	bool InitializePSOs();

	void Update();
	void UpdateObjectConstants();
	void UpdatePassConstants();

	void Draw();
};

