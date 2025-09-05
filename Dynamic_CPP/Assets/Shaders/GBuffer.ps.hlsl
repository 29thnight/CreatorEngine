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
    
    uint bitflag;
}

cbuffer TerrainLayerConstants : register(b12)
{
    int useTerrainLayers;
    float gLayerTiling0;
    float gLayerTiling1;
    float gLayerTiling2;
    float gLayerTiling3;
};


struct PixelShaderInput
{
    float4 position     : SV_POSITION;
    float4 pos          : POSITION0;
    float4 wPosition    : POSITION1;
    float3 normal       : NORMAL;
    float3 tangent      : TANGENT;
    float3 binormal     : BINORMAL;
    float2 texCoord     : TEXCOORD0;
    float2 texCoord1    : TEXCOORD1;
};

struct GBufferOutput
{
    float4 diffuse              : SV_TARGET0;
    float4 metalRoughOcclusion  : SV_TARGET1;
    float4 normal               : SV_TARGET2;
    float4 emissive             : SV_TARGET3;
    uint bitmask                : SV_TARGET4;
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

    uint bit = bitflag;

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
        
        albedo *= gAlbedo;
    }
    
    float occlusion = 1.f;
    float ormAO = 1.f;
    float texAO = 1.f;
    float metallic = gMetallic;
    float roughness = gRoughness;
    float ior = 1;
    [branch]
    if (gUseOccMetalRough)
    {
        float4 occRoughMetal = OcclusionRoughnessMetal.Sample(LinearSampler, IN.texCoord);
        //occRoughMetal = SRGBtoLINEAR(occRoughMetal);
        ormAO = occRoughMetal.r;
        roughness = 1 - occRoughMetal.g;
        metallic = occRoughMetal.b;
    }

    [branch]
    if (gUseAoMap)
    {
        texAO = AoMap.Sample(LinearSampler, IN.texCoord).r;
    }
    
    occlusion = saturate(ormAO * texAO);

    float4 emissive = float4(0.0, 0.0, 0.0, 0.0);
    [branch]
    if (gUseEmmisive)
    {
        emissive = Emissive.Sample(LinearSampler, IN.texCoord);
        if (gConvertToLinear)
            emissive = SRGBtoLINEAR(emissive);

    }

    if (useTerrainLayers)
    {
        float2 uv = IN.texCoord;
        uv.y = -uv.y;
        
        float2 uv0 = uv * gLayerTiling0;
        float2 uv1 = uv * gLayerTiling1;
        float2 uv2 = uv * gLayerTiling2;
        float2 uv3 = uv * gLayerTiling3;
        
        float4 layer0 = LayerAlbedo.SampleLevel(LinearSampler, float3(uv0, (float) 0), 0);
            layer0 = SRGBtoLINEAR(layer0);
        float4 layer1 = LayerAlbedo.SampleLevel(LinearSampler, float3(uv1, (float) 1), 0);
            layer1 = SRGBtoLINEAR(layer1);
        float4 layer2 = LayerAlbedo.SampleLevel(LinearSampler, float3(uv2, (float) 2), 0);
            layer2 = SRGBtoLINEAR(layer2);
        float4 layer3 = LayerAlbedo.SampleLevel(LinearSampler, float3(uv3, (float) 3), 0);
            layer3 = SRGBtoLINEAR(layer3);
        
        
        float4 splat = SplatTexture.Sample(LinearSampler, IN.texCoord);
        splat = normalize(splat);

        float3 color = layer0 * splat.r + layer1 * splat.g + layer2 * splat.b + layer3 * splat.a;

        albedo = float4(color, 1.0);

        occlusion = 1;
        metallic = 0.0;
        roughness = 1.0;
        bit = 1 << 8;
        bit |= 1 << 9; // Set bit 8 and 9 for terrain layers
    }
    
    roughness = max(roughness, 0.1f);
    OUT.diffuse = float4(albedo.rgb, 1);
    OUT.metalRoughOcclusion = float4(metallic, roughness, occlusion, ior);
    float3 normalResult = surf.N * 0.5 + 0.5;
    OUT.normal = float4(normalResult, 1); // 여기 나중에 normal.w 까지 받아서 행렬변환한곳 오류날 가능성 있음.
    OUT.emissive = emissive;

    OUT.bitmask = bit;

    return OUT;
}
