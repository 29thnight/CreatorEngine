#include "BRDF.hlsli"
#include "Sampler.hlsli"

#define VERTEX_NORMALS 0
#define NORMAL_MAP 1
#define BUMP_MAP 2

#define PI 3.14159265358

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float4 pos : POSITION0;
    float4 wPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 texCoord : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

struct SurfaceInfo
{
    float4 posW;
    float3 N;
    float3 T;
    float3 B;
    float3 V;
    float NdotV;
};

struct LightingInfo
{
    // general terms
    float3 L;

    float3 H;
    float NdotH;
    float NdotL;

    //currently for directional light source
    float shadowFactor;

    // for point sources
    float distance;
    float attenuation;
};

cbuffer PBRMaterial : register(b0)
{
    float4 gAlbedo;
    float gMetallic;
    float gRoughness;

    int gUseAlbedoMap;
    int gUseOccMetalRough;
    int gUseAoMap;
    int gUseEmmisive;
    int gNormalState;
    int gConvertToLinear;
}
cbuffer CB : register(b1)
{
    float2 offset;
    float2 size;
    float3 eyePosition;
    int lightmapIndex;
}

Texture2D Albedo : register(t0);
Texture2D Normals : register(t1);
Texture2D OcclusionRoughnessMetal : register(t2);
Texture2D AO : register(t3);
Texture2D Emissive : register(t5);

//Texture2DArray<float4> lightmap : register(t14);
Texture2D<float4> lightmap : register(t14);
Texture2D<float4> directionalmaps : register(t15);

static const float GAMMA = 2.2;
static const float INV_GAMMA = 1.0 / GAMMA;
// sRGB to linear approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
//float4 SRGBtoLINEAR(float4 srgbIn)
//{
//    return float4(pow(srgbIn.xyz, GAMMA), srgbIn.w);
//}
inline float3 CalcNormalFromNormMap(Texture2D normalMap, float2 uv, SurfaceInfo surf)
{
    float3 normalMapVal = normalMap.Sample(LinearSampler, uv).xyz;
    normalMapVal = normalize((normalMapVal * 2) - 1.0);
    float3 N = (normalMapVal.x * surf.T) +
               (normalMapVal.y * surf.B) +
               (normalMapVal.z * surf.N);
    return normalize(N);
}

inline float3 CalcNormalFromBumpMap(Texture2D bumpMap, float2 uv, SurfaceInfo surf)
{
    float3 N = surf.N;
    float2 bumpTextureSize;
    bumpMap.GetDimensions(bumpTextureSize[0], bumpTextureSize[1]);
    float2 pixelSize = 1.0 / bumpTextureSize;

    float mid = bumpMap.Sample(PointSampler, uv) * 2.0 - 1.0;
    float left = bumpMap.Sample(PointSampler, uv + float2(-pixelSize.x, 0)) * 2.0 - 1.0;
    float right = bumpMap.Sample(PointSampler, uv + float2(pixelSize.x, 0)) * 2.0 - 1.0;
    float top = bumpMap.Sample(PointSampler, uv + float2(0, -pixelSize.y)) * 2.0 - 1.0;
    float bottom = bumpMap.Sample(PointSampler, uv + float2(0, pixelSize.y)) * 2.0 - 1.0;
    float3 p1 = ((bottom - mid) - (top - mid)) * normalize(surf.B);
    float3 p2 = ((left - mid) - (right - mid)) * normalize(surf.T);
    return normalize(N - (p1 + p1));
}

float4 main(VertexShaderOutput IN) : SV_TARGET
{
    float4 albedo = gAlbedo;
    float metallic = gMetallic;
    float roughness = gRoughness;
    float occlusion = 1.0;
    float4 emissive = { 0, 0, 0, 0 };
    if (gUseAlbedoMap)
    {

        albedo = Albedo.Sample(LinearSampler, IN.texCoord);
        //if (gConvertToLinear)
        //    albedo = SRGBtoLINEAR(albedo);
    }
    if (gUseOccMetalRough)
    {
        float3 occRoughMetal = OcclusionRoughnessMetal.Sample(LinearSampler, IN.texCoord).rgb;
        occlusion = occRoughMetal.r;
        roughness = occRoughMetal.g;
        metallic = occRoughMetal.b;
    }
    if (gUseEmmisive)
    {
        emissive = Emissive.Sample(LinearSampler, IN.texCoord);
    }
    
    SurfaceInfo surf;
    surf.posW = IN.wPosition;
    surf.T = normalize(IN.tangent);
    surf.B = normalize(IN.binormal);
    surf.N = normalize(IN.normal);
    
    if (gNormalState == NORMAL_MAP)
    {
        surf.N = CalcNormalFromNormMap(Normals, IN.texCoord, surf);
    }
    else if (gNormalState == BUMP_MAP)
    {
        surf.N = CalcNormalFromBumpMap(Normals, IN.texCoord, surf);
    }
    surf.V = normalize(eyePosition.xyz - IN.wPosition.xyz);
    surf.NdotV = dot(surf.N, surf.V);
    
    //float2 lightmapUV = (IN.texCoord1 - offset) / size;
    float2 temp = 1.f / 1024.f * 0.5f;
    float2 lightmapUV = IN.texCoord1 * size + offset;
    //float2 lightmapUV = (IN.texCoord1 + temp) * size + offset;
    float4 lightmapColor = lightmap.SampleLevel(LinearSampler, lightmapUV, 0.0);
    float3 lightDirection = directionalmaps.SampleLevel(LinearSampler, lightmapUV, 0.0).xyz;
    
    lightDirection = normalize(lightDirection);
    float3 Lo = float3(0, 0, 0);
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo.rgb, metallic);
    
    LightingInfo li;
    li.H = normalize(-lightDirection + surf.V);
    li.NdotH = normalize(dot(surf.N, li.H));
    li.NdotL = dot(surf.N, -lightDirection);
    
    float NDF = DistributionGGX(max(0.0, li.NdotH), gRoughness);
    float G = GeometrySmith(max(0.0, surf.NdotV), max(0.0, li.NdotL), gRoughness);
    float3 F = fresnelSchlick(max(dot(li.H, surf.V), 0.0), F0);
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - gMetallic;

    float NdotL = max(li.NdotL, 0.0); // clamped n dot l

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(surf.NdotV, 0.0) * NdotL;
    float3 specular = numerator / max(denominator, 0.001);

    Lo += (kD * albedo.rgb / PI + specular) * lightmapColor.rgb * NdotL;
    
    float3 colour = albedo.rgb * saturate(lightmapColor.rgb) + Lo + emissive.rgb;

    return float4(colour, albedo.a);
    
   // // kD = 1 - F0, F0 is based on metallic
   // float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
   // float3 kD = 1.0 - F0;
   //
   // // Lambert
   // float3 diffuseBRDF = albedo.rgb / PI;
   // 
   // // 라이트맵 기반 직접광
   // float3 directLighting = lightmapColor.rgb;
   //
   // // 최종 조명
   // float3 Lo = kD * diffuseBRDF * directLighting;
   // 
   // return float4(Lo + emissive.rgb, 1);
    
    //lightmapColor = -lightmapColor + 1;
    //float4 finalColor = (albedo / 3.141592) * lightmapColor + emissive;
    //return finalColor;
}