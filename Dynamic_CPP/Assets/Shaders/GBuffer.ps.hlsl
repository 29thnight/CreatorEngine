#include "Lighting.hlsli"
#include "Shading.hlsli"

#define VERTEX_NORMALS 0
#define NORMAL_MAP 1
#define BUMP_MAP 2

Texture2D Albedo : register(t0);
Texture2D NormalMap : register(t1);
Texture2D OcclusionRoughnessMetal : register(t2);
Texture2D AoMap : register(t3);
Texture2D Emissive : register(t5);

Texture2DArray LayerAlbedo : register(t6);
Texture2D SplatTexture : register(t7);

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

cbuffer TerrainLayerConstants : register(b12)
{
    int useTerrainLayers;
    int gLayerIndex;
    float4 gLayerTiling;
};


struct PixelShaderInput
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

struct GBufferOutput
{
    float4 diffuse : SV_TARGET0;
    float4 metalRoughOcclusion : SV_TARGET1;
    float4 normal : SV_TARGET2;
    float4 emissive : SV_TARGET3;
};

GBufferOutput main(PixelShaderInput IN)
{
    GBufferOutput OUT;
    float3 N = normalize(IN.normal);
    SurfaceInfo surf;
    surf.posW = IN.wPosition;
    surf.N = normalize(IN.normal);
    surf.T = normalize(IN.tangent);
    surf.B = normalize(IN.binormal);

    if (gNormalState == NORMAL_MAP)
    {
        surf.N = CalcNormalFromNormMap(NormalMap, IN.texCoord, surf);
    }
    else if (gNormalState == BUMP_MAP)
    {
        surf.N = CalcNormalFromBumpMap(NormalMap, IN.texCoord, surf);
    }
    surf.V = normalize(eyePosition.xyz - surf.posW.xyz);
    surf.NdotV = dot(surf.N, surf.V);

    // PACK GBUFFER
    float4 albedo = gAlbedo;
    [branch]
    if (gUseAlbedoMap)
    {

        albedo = Albedo.Sample(LinearSampler, IN.texCoord);
        if (gConvertToLinear)
            albedo = SRGBtoLINEAR(albedo);
    }
    
    [branch]
    if (useTerrainLayers)
    {
        float2 uv = IN.texCoord * 4096.0;
        uv.y = -uv.y;
        albedo = LayerAlbedo.SampleLevel(LinearSampler, float3(uv, (float)gLayerIndex),0);
        if (gConvertToLinear)
            albedo = SRGBtoLINEAR(albedo);
    }
        
    
    

    float occlusion = 1;

    float metallic = gMetallic;
    float roughness = gRoughness;
    [branch]
    if (gUseOccMetalRough)
    {
        float3 occRoughMetal = OcclusionRoughnessMetal.Sample(LinearSampler, IN.texCoord).rgb;
        occlusion = occRoughMetal.r;
        roughness = occRoughMetal.g;
        metallic = occRoughMetal.b;
    }

    [branch]
    if (gUseAoMap)
    {
        occlusion = AoMap.Sample(LinearSampler, IN.texCoord).r;
    }

    float4 emissive = float4(0.0, 0.0, 0.0, 0.0);
    [branch]
    if (gUseEmmisive)
    {
        emissive = Emissive.Sample(LinearSampler, IN.texCoord);
        if (gConvertToLinear)
            emissive = SRGBtoLINEAR(emissive);

    }


    OUT.diffuse = pow(float4(albedo.rgb, 0), 2.2);
    OUT.metalRoughOcclusion.r = metallic;
    OUT.metalRoughOcclusion.g = roughness;
    OUT.metalRoughOcclusion.b = occlusion;
    OUT.normal = float4(surf.N * 0.5 + 0.5, 1);
    OUT.emissive = emissive;
    return OUT;
}
