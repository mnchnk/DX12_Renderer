#pragma once
#include <DirectXMath.h>

// 입력 레이아웃(mInputLayouts["default"])의 오프셋과 반드시 일치해야 한다.
struct Vertex
{
    DirectX::XMFLOAT3 Pos;      // offset 0
    DirectX::XMFLOAT3 Normal;   // offset 12
    DirectX::XMFLOAT2 TexC;     // offset 24
    DirectX::XMFLOAT3 Tangent;  // offset 32 - 노멀 매핑용 (총 44바이트)
};
