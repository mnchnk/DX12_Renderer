#pragma once
#include <DirectXMath.h>
#include <cmath>

// DirectXMath만 의존하는 순수 수학 헬퍼.
// Material/Light 같은 가벼운 데이터 헤더가 Util.h(=d3d12 전체)를 끌고 오지 않도록
// Util.h에서 분리했다.
class MathHelper
{
public:
    static DirectX::XMFLOAT4X4 Identity4x4()
    {
        return { 1.0f, 0.0f, 0.0f, 0.0f,
                 0.0f, 1.0f, 0.0f, 0.0f,
                 0.0f, 0.0f, 1.0f, 0.0f,
                 0.0f, 0.0f, 0.0f, 1.0f };
    }

    // 주어진 방향을 바라보게 하는 회전 쿼터니언.
    // 기준 forward는 (0,0,1)이고, Transform::GetWorldMatrix와 같은 규약을 쓴다.
    static DirectX::XMFLOAT4 QuaternionFromDirection(
        DirectX::XMFLOAT3 direction,
        DirectX::XMFLOAT3 up = { 0.0f, 1.0f, 0.0f })
    {
        using namespace DirectX;

        XMVECTOR baseForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        XMVECTOR target = XMVector3Normalize(XMLoadFloat3(&direction));

        float dotVal = XMVectorGetX(XMVector3Dot(baseForward, target));

        XMVECTOR quat;
        if (dotVal < -0.9999f) // 거의 정반대 방향이면 축을 못 구하므로 up으로 180도 회전
        {
            quat = XMQuaternionRotationAxis(XMLoadFloat3(&up), XM_PI);
        }
        else
        {
            XMVECTOR axis = XMVector3Normalize(XMVector3Cross(baseForward, target));
            float angle = acosf(dotVal);
            quat = XMQuaternionRotationAxis(axis, angle);
        }

        XMFLOAT4 result;
        XMStoreFloat4(&result, quat);
        return result;
    }
};
