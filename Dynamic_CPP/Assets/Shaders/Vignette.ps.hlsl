#include "Sampler.hlsli"

Texture2D<float4> ColorTexture : register(t0);

cbuffer VignetteCBuffer : register(b0)
{
    float2 RadiusSoftness;
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float4 color = ColorTexture.Sample(PointSampler, IN.texCoord);

    float len = distance(IN.texCoord, float2(0.5, 0.5)) * 0.7f;
    float vignette = smoothstep(RadiusSoftness.r, RadiusSoftness.r - RadiusSoftness.g, len);
    color.rgb *= vignette;
    return color;
}