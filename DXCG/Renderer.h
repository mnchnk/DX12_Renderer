#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <d3d12.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"

class Renderer
{
private:
	std::unique_ptr<GraphicsDevice> mGraphicsDevice;
	std::unique_ptr<CommandQueue> mCommandQueue;
	std::unique_ptr<SwapChain> mSwapChain;

	std::unique_ptr<ID3D12RootSignature> mRootSignature;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;	
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
	
	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;

	UINT mClientWidth = 800;
	UINT mClientHeight = 600;

	HWND mHWnd;

public:
	Renderer(HWND hWnd, UINT clientWidth, UINT clientHeight) :mHWnd(hWnd), mClientWidth(clientWidth), mClientHeight(clientHeight) {}
	~Renderer() = default;

	bool Initialize();
	bool InitializeRootSignature();
	bool InitializeShaders();
	bool InitializePSOs();

	void Update();
	void UpdateObjectConstants();
	void UpdatePassConstants();

	void Draw();
};

