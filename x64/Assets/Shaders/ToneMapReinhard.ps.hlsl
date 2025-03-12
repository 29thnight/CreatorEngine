#include "Sampler.hlsli"
#include "Shading.hlsli"

Texture2D Colour : register(t0);

// Note to self: luma is in sRGB
float CalcLuminance(float3 colour)
{
    return dot(colour.xyz, float3(0.299f, 0.587f, 0.114f));
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    float3 colour = SRGBtoLINEAR(Colour.Sample(PointSampler, IN.texCoord)).rgb;
    float luminance = CalcLuminance(colour);
    float reinhard = luminance / (luminance + 1);
    float3 toneMapped = LINEARtoSRGB(colour * (reinhard / luminance)).rgb;
    
    return float4(toneMapped, 1);
}
