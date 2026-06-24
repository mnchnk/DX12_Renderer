#include "Renderer.h"
#include <memory>
#include <d3d12.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"

bool Renderer::Initialize()
{
	mGraphicsDevice->Initialize();
	mCommandQueue->Initialize(mGraphicsDevice.get());
	mSwapChain->Initialize(mGraphicsDevice.get(), mCommandQueue.get(), mHWnd, mClientWidth, mClientHeight);

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f; 
    mScreenViewport.MaxDepth = 1.0f; 

    mScissorRect.left = 0;
    mScissorRect.top = 0;
    mScissorRect.right = mClientWidth;
    mScissorRect.bottom = mClientHeight;

    return true;
}

bool Renderer::InitializeRootSignature()
{
    return false;
}

bool Renderer::InitializeShaders()
{
    return false;
}

bool Renderer::InitializePSOs()
{
    return false;
}

void Renderer::Update()
{
}

void Renderer::Draw()
{
}
