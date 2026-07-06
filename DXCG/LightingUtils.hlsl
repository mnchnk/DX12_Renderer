#define MaxLights 16
#define PI       3.14159265359f
#define PI2      6.28318530718f  // PI * 2
#define INV_PI   0.31830988618f  // 1 / PI

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

float3 SchlickFresnel(float3 R0, float3 halfVec, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(halfVec, lightVec));
    float f0 = 1.0f - cosIncidentAngle;

    float f0_5 = f0 * f0 * f0 * f0 * f0;
    return R0 + (1.0f - R0) * f0_5;
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 ComputeBRDF(Light L, Material mat, float3 normal, float3 viewVec, float3 lightVec)
{
    float3 halfVec = normalize(lightVec + viewVec);
    float NdotL = max(dot(normal, lightVec), 0.0f);
    float NdotV = max(dot(normal, viewVec), 0.0f);

    float D = DistributionGGX(normal, halfVec, mat.Roughness);
    float3 F = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);
    float G = GeometrySmith(normal, viewVec, lightVec, mat.Roughness);

    float3 specular = (D * F * G) / max(4.0f * NdotV * NdotL, 0.001f);
    float3 kD = 1.0f - F;
    float3 diffuse = kD * mat.DiffuseAlbedo.rgb * INV_PI;

    return (diffuse + specular) * NdotL;
}

// 1. 방향광
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 viewVec)
{
    float3 lightVec = normalize(-L.Direction);
    float3 brdf = ComputeBRDF(L, mat, normal, viewVec, lightVec);
    
    return brdf * L.Strength;
}

// 2. 점광원
float3 ComputePointLight(Light L, Material mat, float3 posW, float3 normal, float3 viewVec)
{
    float3 lightVec = L.Position - posW;
    float d = length(lightVec);
    
    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);
    
    lightVec /= d; 
    
    float atten = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    float3 brdf = ComputeBRDF(L, mat, normal, viewVec, lightVec);
    
    return brdf * L.Strength * atten;
}

// 3. 스포트라이트
float3 ComputeSpotLight(Light L, Material mat, float3 posW, float3 normal, float3 viewVec)
{
    float3 lightVec = L.Position - posW;
    float d = length(lightVec);
    
    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);
    
    lightVec /= d; // 정규화
    
    float atten = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    
    float3 brdf = ComputeBRDF(L, mat, normal, viewVec, lightVec);
    
    return brdf * L.Strength * atten * spotFactor;
}