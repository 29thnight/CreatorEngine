#include "Sampler.hlsli"

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float4 pos : POSITION0;
    float4 wPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 texCoord : TEXCOORD0;
};

cbuffer CB : register(b0)
{
    float2 offset;
    float2 size;
    int lightmapIndex;
}

Texture2DArray<float4> lightmap : register(t14);

float4 main(VertexShaderOutput IN) : SV_Target
{
    //float2 lightmapUV = (IN.texCoord - offset) / size;
    float2 lightmapUV = IN.texCoord * size + offset;
    float4 lightmapColor = lightmap.SampleLevel(LinearSampler, float3(lightmapUV, lightmapIndex), 0.0);

    //lightmapColor = -lightmapColor + 1;
    
    return lightmapColor;
}