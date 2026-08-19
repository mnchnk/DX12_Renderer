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
    uint NormalMapIndex;
    uint MatPad1;
    uint MatPad2;
};

StructuredBuffer<MaterialData> gMaterialData : register(t0);
Texture2D gShadowMap : register(t1);
Texture2D gTextures[] : register(t2);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentL : TANGENT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentW : TANGENT;
    float4 ShadowPosH : POSITION1;
};

// Turns a tangent space sample from a normal map into a world space normal.
float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    // Texture stores [0,1]; the vector we want is [-1,1].
    float3 normalT = 2.0f * normalMapSample - 1.0f;

    // Gram-Schmidt: interpolation can leave the tangent slightly off the normal.
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);
    return normalize(mul(normalT, TBN));
}

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
    vout.TangentW = mul(vin.TangentL, (float3x3)gWorld);
    vout.PosH = mul(posW, gViewProj);

    // Without this every pixel samples texel (0,0).
    vout.TexC = vin.TexC;

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

    // An index of -1 arrives here as 0xFFFFFFFF because the field is uint.
    // Sampling with it would read far outside the array and hang the GPU.
    if (matData.DiffuseMapIndex != 0xFFFFFFFF)
    {
        mat.DiffuseAlbedo *= gTextures[NonUniformResourceIndex(matData.DiffuseMapIndex)]
                                .Sample(gsamAnisotropicWrap, pin.TexC);
    }

    if (matData.NormalMapIndex != 0xFFFFFFFF)
    {
        float3 normalSample = gTextures[NonUniformResourceIndex(matData.NormalMapIndex)]
                                .Sample(gsamAnisotropicWrap, pin.TexC).rgb;

        N = NormalSampleToWorldSpace(normalSample, N, pin.TangentW);
    }


    float shadowFactor = CalcShadowFactor(pin.ShadowPosH);
    float3 finalColor = float3(0.0f, 0.0f, 0.0f);

    int i = 0;

    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        finalColor += shadowFactor * ComputeDirectionalLight(gLights[i], mat, N, V);
    }
    
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        finalColor += ComputePointLight(gLights[i], mat, pin.PosW, N, V);
    }

    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        finalColor += ComputeSpotLight(gLights[i], mat, pin.PosW, N, V);
    }

    float3 ambient = gAmbientLight.rgb * mat.DiffuseAlbedo.rgb;
    finalColor += ambient;
    
    finalColor = pow(finalColor, float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));

    return float4(finalColor, mat.DiffuseAlbedo.a);
}