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

// 뼈대를 선으로 그리기 위한 데이터.
// 정점 버퍼 없이 SV_VertexID로 이 배열을 인덱싱한다.
struct BoneDebugConstants
{
    DirectX::XMFLOAT4X4 BoneWorld[96];     // 본의 모델 공간 위치/축
    DirectX::XMINT4     BoneParent[96];    // .x = 부모 인덱스, -1이면 루트
    DirectX::XMFLOAT4X4 RootWorld;         // 캐릭터 오브젝트의 월드 행렬
    UINT  BoneCount = 0;
    float AxisLength = 0.05f;              // 관절 축 표시 길이 (월드 단위)
    UINT  Pad[2] = {};
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

        // 디버그용은 항상 한 벌만 있으면 된다.
        BoneDebugCB = std::make_unique<UploadBuffer<BoneDebugConstants>>(device, 1, true);
    }

    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() = default;

    ComPtr<ID3D12CommandAllocator> CmdAllocator;
    
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
    std::unique_ptr<UploadBuffer<SkinnedConstants>> SkinnedCB = nullptr;
    std::unique_ptr<UploadBuffer<BoneDebugConstants>> BoneDebugCB = nullptr;
    UINT64 Fence = 0;
};