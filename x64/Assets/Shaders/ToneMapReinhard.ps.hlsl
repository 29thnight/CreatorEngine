#include "Sampler.hlsli"

static const float GAMMA = 2.2f;

Texture2D Colour : register(t0);

// Note to self: luma is in sRGB
float ColorToLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float3 ReinhardToneMapping(float3 color)
{
    float luma = ColorToLuminance(color);
    float toneMappedLuma = luma / (1. + luma);
    if (luma > 1e-6)
        color *= toneMappedLuma / luma;
    
    color = pow(color, 1. / GAMMA);
    return color;
}

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float3 colour = Colour.Sample(PointSampler, IN.texCoord).rgb;
    float4 toneMapedColor = float4(ReinhardToneMapping(colour), 1.f);
    
    return toneMapedColor;
}
