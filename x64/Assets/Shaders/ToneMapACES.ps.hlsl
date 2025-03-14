#include "Sampler.hlsli"
#include "Shading.hlsli"

Texture2D Colour : register(t0);

cbuffer UseTonemap : register(b0)
{
    bool useTonemap;
    float shoulderStrength;
    float linearStrength;
    float linearAngle;
    float teoStrength;
    float toeNumerator;
    float toeDenominator;
}

float3 Tonemap_Curve(float3 color, 
                     float ShoulderStrength, 
                     float LinearStrength, 
                     float LinearAngle, 
                     float TeoStrength, 
                     float ToeNumerator, 
                     float ToeDenominator
)
{
    return saturate((color.rgb * 
    (ShoulderStrength * color.rgb + LinearStrength * LinearAngle) + TeoStrength * ToeNumerator) 
    / (color.rgb * (ShoulderStrength * color.rgb + LinearStrength) + TeoStrength * ToeDenominator) - 
    ToeNumerator / ToeDenominator);
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
        toneMapped = Tonemap_Curve(colour, 
                                   shoulderStrength, 
                                   linearStrength, 
                                   linearAngle, 
                                   teoStrength, 
                                   toeNumerator, 
                                   toeDenominator
        );
    }
    else
    {
        toneMapped = colour;
    }
    
    return float4(toneMapped, 1);
}