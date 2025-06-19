#include "Sampler.hlsli"
Texture2D targetTexture : register(t0);

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

cbuffer gBrush : register(b0)
{
    float2 gBrushWorldPosition; // Brush position in UV space
    float gBrushRadius; // Brush radius in UV space
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 clipPos = IN.pos;
    float3 ndc = clipPos.xyz / clipPos.w;
    float2 uv = 0.5 * ndc.xy + 0.5; // Convert NDC to UV space
    uv.y = -uv.y;
    //uv.y = 1.0 - uv.y;

    float dist = length(IN.wPosition.xz - gBrushWorldPosition);
    float invBrushRadius = 1.0 / gBrushRadius;
    float clampRadius = dist * invBrushRadius;
    
    float3 tex= targetTexture.Sample(LinearSampler, uv);
  
    float3 color = tex;

    if (dist < gBrushRadius)
    {
        // If within brush radius, return a color (e.g., white)
        
        color = lerp(float3(0, clampRadius, 1.0 - clampRadius), tex, 0.8);
    }
    
    return float4(color, 1);
}