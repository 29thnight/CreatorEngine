#include "Sampler.hlsli"
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
    int lightmapIndex;
}

Texture2D Albedo : register(t0);
Texture2D Normals : register(t1);
Texture2D OcclusionRoughnessMetal : register(t2);
Texture2D AO : register(t3);
Texture2D Emissive : register(t4);

Texture2DArray<float4> lightmap : register(t14);

static const float GAMMA = 2.2;
static const float INV_GAMMA = 1.0 / GAMMA;
// sRGB to linear approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
//float4 SRGBtoLINEAR(float4 srgbIn)
//{
//    return float4(pow(srgbIn.xyz, GAMMA), srgbIn.w);
//}

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
    //float2 lightmapUV = (IN.texCoord1 - offset) / size;
    float2 lightmapUV = IN.texCoord1 * size + offset;
    float4 lightmapColor = lightmap.SampleLevel(LinearSampler, float3(lightmapUV, lightmapIndex), 0.0);
    
    // kD = 1 - F0, F0 is based on metallic
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    float3 kD = 1.0 - F0;

    // Lambert
    float3 diffuseBRDF = albedo.rgb / PI;
    
    // 라이트맵 기반 직접광
    float3 directLighting = lightmapColor.rgb;

    // 최종 조명
    float3 Lo = kD * diffuseBRDF * directLighting;
    
    return float4(Lo + emissive.rgb, 1);
    
    //lightmapColor = -lightmapColor + 1;
    //float4 finalColor = (albedo / 3.141592) * lightmapColor + emissive;
    //return finalColor;
}