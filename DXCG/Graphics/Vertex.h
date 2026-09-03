#pragma once
#include <DirectXMath.h>
#include <Windows.h>   // UINT

// 정점 하나가 영향받을 수 있는 본의 최대 개수.
// 셰이더 루프 횟수와 입력 레이아웃이 이 값에 맞춰져 있다.
static const int MaxBoneInfluence = 4;

// 입력 레이아웃(mInputLayouts["default"])의 오프셋과 반드시 일치해야 한다.
struct Vertex
{
    DirectX::XMFLOAT3 Pos;      // offset 0
    DirectX::XMFLOAT3 Normal;   // offset 12
    DirectX::XMFLOAT2 TexC;     // offset 24
    DirectX::XMFLOAT3 Tangent;  // offset 32 - 노멀 매핑용

    // 스키닝용. 정적 메시는 전부 0으로 두고 일반 VS를 쓰면 된다.
    // (가중치가 모두 0인 채로 스킨드 VS를 타면 정점이 원점으로 뭉개진다)
    UINT  BoneIndices[MaxBoneInfluence] = { 0, 0, 0, 0 };   // offset 44
    float BoneWeights[MaxBoneInfluence] = { 0.0f, 0.0f, 0.0f, 0.0f }; // offset 60
    // 총 76바이트
};
