#pragma once
#include <DirectXMath.h>
#include <string>
#include <Windows.h>   // UINT
#include "Core/MathHelper.h"
#include "Graphics/GraphicsCommon.h"

// GPU로 넘어가는 레이아웃. Default.hlsl의 MaterialData와 1:1로 대응해야 하므로
// Name이나 NumFramesDirty 같은 CPU 전용 필드를 여기에 추가하면 안 된다.
struct MaterialData
{
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

    // -1은 "이 맵이 없다"는 뜻. uint로 넘어가면 0xFFFFFFFF가 되고
    // 셰이더가 그 값을 보고 샘플링을 건너뛴다. 0으로 두면 0번 텍스처를 읽어버린다.
    UINT DiffuseMapIndex = (UINT)-1;
    UINT NormalMapIndex = (UINT)-1;
    UINT MaterialPad1 = 0;
    UINT MaterialPad2 = 0;
};

// CPU 쪽 머티리얼. 이름, 버퍼 인덱스, 더티 플래그 등 엔진이 필요로 하는 것을 담는다.
struct Material
{
    std::string Name;

    int MatCBIndex = -1;            // MaterialBuffer 안의 슬롯
    int DiffuseSrvHeapIndex = -1;   // SRV 힙 안의 위치. -1이면 텍스처 없음
    int NormalSrvHeapIndex = -1;
    int NumFramesDirty = MaxFrameResource;

    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
    float Roughness = 0.25f;
    DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};
