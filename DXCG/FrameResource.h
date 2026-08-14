#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include "Util.h"
#include <wrl.h>
#include <memory>

using Microsoft::WRL::ComPtr;

// LightingUtils.hlsl의 MaxLights와 반드시 일치해야 함
#define MAXLIGHT 16
static const int MaxFrameResource = 3;

// Default.hlsl의 NUM_DIR_LIGHTS / NUM_POINT_LIGHTS / NUM_SPOT_LIGHTS와 반드시 일치해야 함
// gLights 배열에서 각 타입이 차지하는 슬롯 범위를 결정한다.
static const int NumDirLights = 1;
static const int NumPointLights = 1;
static const int NumSpotLights = 0;

enum class LightType
{
    Directional = 0,
    Point,
    Spot
};

// GPU로 넘어가는 레이아웃. LightingUtils.hlsl의 Light 구조체와 1:1로 대응해야 하므로
// 타입 정보 같은 CPU 전용 필드를 여기에 추가하면 안 된다. (Material / MaterialData 관계와 동일)
struct LightData
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;
};

// CPU 쪽 라이트. 타입 정보를 들고 있어서 어느 슬롯에 들어가야 하는지,
// Transform에서 어떤 성분(위치/방향)을 가져와야 하는지 판단할 수 있다.
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
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
};

struct FrameResource
{
public:
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
    {
        // 프레임마다 전용 얼로케이터를 둔다. 얼로케이터는 그것으로 기록한 커맨드 리스트의
        // GPU 실행이 모두 끝난 뒤에만 Reset할 수 있는데, 하나를 공유하면 아직 실행 중인
        // 이전 프레임의 커맨드를 밟게 된다.
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