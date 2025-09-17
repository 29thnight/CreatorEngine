#include "Lighting.hlsli"
#include "Shading.hlsli"

#define VERTEX_NORMALS 0
#define NORMAL_MAP 1
#define BUMP_MAP 2

#define MAX_TERRAIN_LAYERS 16

Texture2D Albedo : register(t0);
Texture2D NormalMap : register(t1);
Texture2D OcclusionRoughnessMetal : register(t2);
Texture2D AoMap : register(t3);
Texture2D Emissive : register(t5);

Texture2DArray LayerAlbedo : register(t6);
//Texture2D SplatTexture : register(t7);
Texture2DArray SplatMapArray : register(t7);

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
    int numLayers; // 실제 사용하는 레이어 개수
    int2 padding;
    float gLayerTiling[MAX_TERRAIN_LAYERS];
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

    if (useTerrainLayers && numLayers > 0)
    {
        float3 finalColor = float3(0.0f, 0.0f, 0.0f);
        float totalWeight = 0.0f;

        // 루프를 통해 모든 레이어를 순회하며 색상을 계산하고 가중치를 합산
        [loop]
        for (int i = 0; i < numLayers; ++i)
        {
            float2 uv = IN.texCoord * gLayerTiling[i];
            uv.y = -uv.y;

            // 레이어의 Albedo 색상 샘플링
            float3 layerColor = LayerAlbedo.Sample(LinearSampler, float3(uv, (float) i)).rgb;
            layerColor = SRGBtoLINEAR(float4(layerColor, 1.0)).rgb;

            // SplatMapArray에서 해당 레이어의 가중치(Weight) 샘플링
            float weight = SplatMapArray.Sample(LinearSampler, float3(IN.texCoord, (float) i)).r;

            finalColor += layerColor * weight;
            totalWeight += weight;
        }

        // 가중치의 합이 0이 되는 경우를 방지 (검은색 픽셀 방지)
        if (totalWeight < 0.001f)
        {
            // 베이스 텍스처(0번)를 기본 색상으로 사용
            float2 uv = IN.texCoord * gLayerTiling[0];
            uv.y = -uv.y;
            finalColor = LayerAlbedo.Sample(LinearSampler, float3(uv, 0.0f)).rgb;
            finalColor = SRGBtoLINEAR(float4(finalColor, 1.0)).rgb;
        }
        else
        {
            // 가중치 합으로 나누어 정규화 (선택적)
            // finalColor /= totalWeight;
        }

        albedo = float4(finalColor, 1.0);

        occlusion = 1;
        metallic = 0.0;
        roughness = 1.0;
        bit = 1 << 8;
        bit |= 1 << 9; // 터레인 레이어 비트 설정
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
