//=============================================================================
// 스켈레톤 디버그 뷰 - 기하 셰이더 연습
//
// 정점 버퍼가 없다. 본 개수만큼 '점'을 그려달라고 요청하면(DrawInstanced),
// VS가 SV_VertexID로 상수 버퍼에서 본 정보를 읽고,
// GS가 그 점 하나를 선 4개(정점 8개)로 부풀린다.
//
//   점 1개  ->  뼈대 선 1개 (부모 -> 자식)
//           +  관절 축 3개 (X 빨강 / Y 초록 / Z 파랑)
//
// GS가 없다면 매 프레임 CPU에서 선분 정점 버퍼를 다시 만들어 올려야 한다.
// 본 데이터는 이미 상수 버퍼로 GPU에 있으니, 기하 생성만 GPU로 넘기는 것이다.
//=============================================================================

// PassConstants의 앞부분만 선언했다. 상수 버퍼 선언이 실제 버퍼보다 짧아도 된다.
cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
};

// C++의 BoneDebugConstants와 바이트 단위로 일치해야 한다.
cbuffer cbBoneDebug : register(b3)
{
    float4x4 gBoneWorld[96];
    int4 gBoneParent[96];    // .x만 쓴다. HLSL 배열은 어차피 16바이트로 정렬된다
    float4x4 gRootWorld;
    uint gBoneCount;
    float gAxisLength;
    uint2 gBoneDebugPad;
};

struct VertexOut
{
    float3 PosW : POSITION0;    // 이 본의 월드 위치
    float3 ParentW : POSITION1; // 부모 본의 월드 위치
    float3 AxisX : TANGENT0;
    float3 AxisY : TANGENT1;
    float3 AxisZ : TANGENT2;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 Color : COLOR;
};

// 행벡터 규약이므로 4행이 이동 성분, 1~3행이 각 축 방향이다.
float3 GetTranslation(float4x4 m)
{
    return m._41_42_43;
}

VertexOut VS(uint vid : SV_VertexID)
{
    VertexOut vout;

    float4x4 boneW = mul(gBoneWorld[vid], gRootWorld);
    vout.PosW = GetTranslation(boneW);

    int parent = gBoneParent[vid].x;
    if (parent >= 0)
    {
        float4x4 parentW = mul(gBoneWorld[parent], gRootWorld);
        vout.ParentW = GetTranslation(parentW);
    }
    else
    {
        // 루트는 부모가 없다. 자기 자신을 넣으면 길이 0인 선이 되어 안 보인다.
        vout.ParentW = vout.PosW;
    }

    vout.AxisX = normalize(boneW._11_12_13);
    vout.AxisY = normalize(boneW._21_22_23);
    vout.AxisZ = normalize(boneW._31_32_33);

    return vout;
}

// maxvertexcount는 이 GS가 만들어낼 수 있는 최대 정점 수.
// 선 4개 x 2정점 = 8. 이 값을 넘으면 컴파일 에러가 난다.
[maxvertexcount(8)]
void GS(point VertexOut gin[1], inout LineStream<GeoOut> lines)
{
    VertexOut v = gin[0];
    GeoOut o;

    // 1. 뼈대 - 부모 관절에서 이 관절까지
    o.Color = float3(1.0f, 1.0f, 0.0f);
    o.PosH = mul(float4(v.ParentW, 1.0f), gViewProj);
    lines.Append(o);
    o.PosH = mul(float4(v.PosW, 1.0f), gViewProj);
    lines.Append(o);

    // 선분을 끊는다. 안 하면 다음 Append가 이어진 폴리라인이 되어버린다.
    lines.RestartStrip();

    // 2~4. 관절의 로컬 축. 회전이 제대로 적용됐는지 눈으로 확인할 수 있다.
    o.Color = float3(1.0f, 0.0f, 0.0f);
    o.PosH = mul(float4(v.PosW, 1.0f), gViewProj);
    lines.Append(o);
    o.PosH = mul(float4(v.PosW + v.AxisX * gAxisLength, 1.0f), gViewProj);
    lines.Append(o);
    lines.RestartStrip();

    o.Color = float3(0.0f, 1.0f, 0.0f);
    o.PosH = mul(float4(v.PosW, 1.0f), gViewProj);
    lines.Append(o);
    o.PosH = mul(float4(v.PosW + v.AxisY * gAxisLength, 1.0f), gViewProj);
    lines.Append(o);
    lines.RestartStrip();

    o.Color = float3(0.0f, 0.0f, 1.0f);
    o.PosH = mul(float4(v.PosW, 1.0f), gViewProj);
    lines.Append(o);
    o.PosH = mul(float4(v.PosW + v.AxisZ * gAxisLength, 1.0f), gViewProj);
    lines.Append(o);
    lines.RestartStrip();
}

float4 PS(GeoOut pin) : SV_Target
{
    return float4(pin.Color, 1.0f);
}
