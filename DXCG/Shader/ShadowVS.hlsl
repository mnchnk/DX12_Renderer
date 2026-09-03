cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
}

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float4x4 gLightView;
    float4x4 gLightProj;
    float4x4 gLightViewProj;
};

#ifdef SKINNED
cbuffer cbSkinned : register(b2)
{
    float4x4 gBoneTransforms[96];
};
#endif

struct VertexIn
{
    float3 PosL : POSITION;

#ifdef SKINNED
    uint4 BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
#endif
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float3 posL = vin.PosL;

#ifdef SKINNED
    // 메인 패스와 똑같이 스키닝해야 그림자가 캐릭터를 따라간다.
    // 여기서 빼먹으면 그림자만 T포즈로 굳는다.
    posL = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        posL += vin.BoneWeights[i] * mul(float4(vin.PosL, 1.0f), gBoneTransforms[vin.BoneIndices[i]]).xyz;
    }
#endif

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    vout.PosH = mul(posW, gLightViewProj);

    return vout;
}
