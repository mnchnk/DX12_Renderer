#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include "Util.h"
#include <wrl.h>
#include <memory>

using Microsoft::WRL::ComPtr;

// Must match MaxLights in LightingUtils.hlsl.
#define MAXLIGHT 16
static const int MaxFrameResource = 3;

// Must match NUM_DIR_LIGHTS / NUM_POINT_LIGHTS / NUM_SPOT_LIGHTS in Default.hlsl.
// These decide which slot range of gLights each light type occupies.
static const int NumDirLights = 1;
static const int NumPointLights = 1;
static const int NumSpotLights = 0;

enum class LightType
{
    Directional = 0,
    Point,
    Spot
};

// GPU-facing layout. Must map 1:1 onto the Light struct in LightingUtils.hlsl,
// so never add CPU-only fields (like a type tag) here.
// Same relationship as Material / MaterialData.
struct LightData
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;
};

// CPU-side light. Carries the type tag so we can decide which gLights slot it
// belongs in, and which Transform component (position / direction) to read.
struct Light
{
    LightType Type = LightType::Directional;

    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;

    LightData ToLightData() const
    {
        LightData data;
        data.Strength = Strength;
        data.FalloffStart = FalloffStart;
        data.Direction = Direction;
        data.FalloffEnd = FalloffEnd;
        data.Position = Position;
        data.SpotPower = SpotPower;
        return data;
    }
};

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

struct MaterialData
{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    UINT DiffuseMapIndex = 0;
    UINT NormalMapIndex;
    UINT MaterialPad1;
    UINT MaterialPad2;
};

struct Material
{
    std::string Name;
    int MatCBIndex = -1;
    int DiffuseSrvHeapIndex = -1;
    int NormalSrvHeapIndex = -1;
    int NumFramesDirty = MaxFrameResource;

    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Vertex
{
	DirectX::XMFLOAT3 Pos;      // offset 0
	DirectX::XMFLOAT3 Normal;   // offset 12
	DirectX::XMFLOAT2 TexC;     // offset 24
	DirectX::XMFLOAT3 Tangent;  // offset 32 - tangent for normal mapping (44 bytes total)
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
    {
        // One allocator per frame. An allocator may only be Reset once the GPU has
        // finished every command list recorded from it, so sharing a single one
        // would stomp on commands from a frame still in flight.
        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocator.GetAddressOf())));

        PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
        ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
        MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdAllocator.GetAddressOf())));
    }

    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() = default;

    ComPtr<ID3D12CommandAllocator> CmdAllocator;
    
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
    
    UINT64 Fence = 0;
};