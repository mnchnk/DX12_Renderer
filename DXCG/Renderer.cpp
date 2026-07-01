#include "Renderer.h"
#include <memory>
#include <d3d12.h>
#include "d3dx12.h"
#include <DirectXMath.h>
#include <DirectXColors.h>
#include "GraphicsDevice.h"
#include "CommandQueue.h"
#include "SwapChain.h"
#include "Util.h"

using namespace DirectX;

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
    
    InitializeFrameResource();
    InitializeRootSignature();
    InitializeShadersAndInputLayout();
    InitializePSOs();

    ID3D12GraphicsCommandList* cmdList = mCommandQueue->GetCommandList();
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList };
    mCommandQueue->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    mCommandQueue->FlushCommandQueue();

    return true;
}

bool Renderer::InitializeFrameResource()
{
    for (int i = 0; i < MaxFrameResource; i++)
    {
        mFrameResources[i] = std::make_unique<FrameResource>(mGraphicsDevice->GetDevice(), 3, 3, 3);
    }

    mCurrFrameResourceIndex = 0;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
    return true;
}

bool Renderer::InitializeRootSignature()
{
    return false;
}

bool Renderer::InitializeShadersAndInputLayout()
{
    mShaders["standardVS"] = CompileShader(L"VertexShader.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["standardPS"] = CompileShader(L"FragmentShader.hlsl", nullptr, "PS", "ps_5_1");

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
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), mShaders["standardVS"]->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["standardPS"]->GetBufferPointer()), mShaders["standardPS"]->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    ThrowIfFailed(mGraphicsDevice->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    return false;
}

void Renderer::Update()
{
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % 3;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mCommandQueue->GetFence()->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mCommandQueue->GetFence()->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectConstants();
    UpdatePassConstants();
    UpdateCamera();
    

}

void Renderer::UpdateObjectConstants()
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& e : mAllRenderItems)
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
            
            currObjectCB->CopyData(e->ObjectCBIndex, objConstants);

            e->NumFramesDirty--;
        }
    }
}

void Renderer::UpdatePassConstants()
{
    XMMATRIX view = mMainCamera.GetView();
    XMMATRIX proj = mMainCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mPassCB.EyePosW = mMainCamera.GetPosition3f();
    mPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mPassCB.NearZ = 1.0f;
    mPassCB.FarZ = 1000.0f;

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mPassCB);
}

void Renderer::UpdateMaterialBuffer()
{

}

void Renderer::UpdateCamera()
{
    if (mMainCamera.mViewDirty)
    {
        mMainCamera.UpdateViewMatrix();
    }
}

void Renderer::Draw()
{
    auto cmdAllocator = mCommandQueue->GetCommandAllocator();
    auto commandList = mCommandQueue->GetCommandList();

    ThrowIfFailed(cmdAllocator->Reset());
    ThrowIfFailed(commandList->Reset(cmdAllocator, mPSOs["opaque"].Get()));
    commandList->RSSetViewports(1, &mScreenViewport);
    commandList->RSSetScissorRects(1, &mScissorRect);

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain->GetCurrentRenderTarget(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    commandList->ClearRenderTargetView(mSwapChain->GetCurrentRtvHandle(), Colors::LightSteelBlue, 0, nullptr);
    commandList->ClearDepthStencilView(mSwapChain->GetCurrentDsvHandle(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    
    commandList->OMSetRenderTargets(1, &mSwapChain->GetCurrentRtvHandle(), true, &mSwapChain->GetCurrentDsvHandle());
  
    commandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain->GetCurrentRenderTarget(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(commandList->Close());

    ID3D12CommandList* cmdsLists[] = { commandList };
    mCommandQueue->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    mSwapChain->Present();
    mCurrFrameResource->Fence = ++mCommandQueue->mCurrFence;

    mCommandQueue->GetCommandQueue()->Signal(mCommandQueue->GetFence(), mCommandQueue->mCurrFence);
}
