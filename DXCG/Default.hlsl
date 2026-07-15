#include "LightingUtils.hlsl"

#define NUM_DIR_LIGHTS 1
#define NUM_POINT_LIGHTS 1
#define NUM_SPOT_LIGHTS 0

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
    uint gMaterialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

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
    
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];
};

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint MatPad0;
    uint MatPad1;
    uint MatPad2;
};

StructuredBuffer<MaterialData> gMaterialData : register(t0);
Texture2D gShadowMap : register(t1);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float4 ShadowPosH : POSITION1;
};

float CalcShadowFactor(float4 shadowPosH)
{
    shadowPosH.xyz /= shadowPosH.w;
    
    float currentDepth = shadowPosH.z;
    
    float2 shadowUV = shadowPosH.xy * float2(0.5f, -0.5f) + 0.5f;
    
    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f ||
        currentDepth > 1.0f)
    {
        return 1.0f;
    }
    
    return gShadowMap.SampleCmpLevelZero(gsamShadow, shadowUV, currentDepth).r;
}


VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
      
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorld);
    vout.PosH = mul(posW, gViewProj);
    
    vout.ShadowPosH = mul(posW, gLightViewProj);
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    
    float3 N = normalize(pin.NormalW);
    float3 V = normalize(gEyePosW - pin.PosW);

    MaterialData matData = gMaterialData[gMaterialIndex];
    
    Material mat;
    mat.DiffuseAlbedo = matData.DiffuseAlbedo;
    mat.FresnelR0 = matData.FresnelR0;
    mat.Roughness = matData.Roughness;

    float shadowFactor = CalcShadowFactor(pin.ShadowPosH);
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);

    int i = 0;

    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        finalColor += shadowFactor * ComputeDirectionalLight(gLights[i], mat, N, V);
    }
    
    // 1. 방향광 누적 계산
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        finalColor += ComputeDirectionalLight(gLights[i], mat, N, V);
    }

    // 2. 점광원 누적 계산
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        finalColor += ComputePointLight(gLights[i], mat, pin.PosW, N, V);
    }

    // 3. 스포트라이트 누적 계산
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        finalColor += ComputeSpotLight(gLights[i], mat, pin.PosW, N, V);
    }

    // 간접광(Ambient) 추가 및 감마 보정
    float3 ambient = gAmbientLight.rgb * mat.DiffuseAlbedo.rgb;
    finalColor += ambient;
    
    // HDR 색상을 LDR(모니터)로 맞추기 위한 Tone Mapping이 들어간다면 여기서 수행합니다.
    finalColor = pow(finalColor, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, mat.DiffuseAlbedo.a);
}