#include "Sampler.hlsli"
#include "Shading.hlsli"

Texture2D Colour : register(t0);

cbuffer UseTonemap : register(b0)
{
    bool useTonemap;
    float filmSlope;
    float filmToe;
    float filmShoulder;
    float filmBlackClip;
    float filmWhiteClip;
}

float3 AcesToneMap_UE4(float3 acesColour)
{
    return saturate((acesColour * (filmSlope * acesColour + filmToe)) / 
    (acesColour * (filmShoulder * acesColour + filmBlackClip) + filmWhiteClip));
}

struct PixelShaderInput // see Fullscreen.vs.hlsl
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{

    
    float3 colour = Colour.Sample(PointSampler, IN.texCoord).rgb;
    float3 toneMapped = 0;
    [branch]
    if (useTonemap)
    {
        toneMapped = AcesToneMap_UE4(colour);
    }
    else
    {
        toneMapped = colour;
    }
    
    return float4(toneMapped, 1);
}