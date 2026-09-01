#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include "Graphics/Util.h"
#include <wrl.h>
#include <memory>
#include "Graphics/Light.h"
#include "Graphics/Material.h"

using Microsoft::WRL::ComPtr;

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    
    UINT MaterialIndex = -1;
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 LightView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 LightProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 LightViewProj = MathHelper::Identity4x4();

    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };
    
    LightData Lights[MAXLIGHT];
};

struct SkinnedConstants
{
    DirectX::XMFLOAT4X4 BoneTransforms[96];
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT SkinCount)
    {
        // One allocator per frame. An allocator may only be Reset once the GPU has
        // finished every command list recorded from it, so sharing a single one
        // would stomp on commands from a frame still in flight.
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocator.GetAddressOf())));

        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
        ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
        MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
        // CBV로 바인딩하므로 true. false면 256바이트 정렬이 안 돼서
        // SetGraphicsRootConstantBufferView가 실패한다.
        SkinnedCB = std::make_unique<UploadBuffer<SkinnedConstants>>(device, SkinCount, true);
    }

    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() = default;

    ComPtr<ID3D12CommandAllocator> CmdAllocator;
    
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
    std::unique_ptr<UploadBuffer<SkinnedConstants>> SkinnedCB = nullptr;
    UINT64 Fence = 0;
};