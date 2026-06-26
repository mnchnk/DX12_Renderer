#include "Renderer.h"
#include <memory>
#include <d3d12.h>
#include <DirectXMath.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"
#include "Util.h"

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
    
    ID3D12GraphicsCommandList* cmdList = mCommandQueue->GetCommandList();
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList };
    mCommandQueue->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    mCommandQueue->FlushCommandQueue();

    return true;
}

bool Renderer::InitializeRootSignature()
{
    return false;
}

bool Renderer::InitializeShadersAndInputLayout()
{
    //mShaders["standardVS"] = CompileShader();
    //mShaders['standardPS"] = CompileShader();

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    return true;
}

bool Renderer::InitializePSOs()
{
    return false;
}

void Renderer::Update()
{
}

void Renderer::UpdateObjectConstants()
{
}

void Renderer::UpdatePassConstants()
{
}

void Renderer::Draw()
{
}
