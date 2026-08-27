#pragma once
#include <DirectXMath.h>
#include "Graphics/GraphicsCommon.h"

enum class LightType
{
    Directional = 0,
    Point,
    Spot
};

struct LightData
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;
};

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