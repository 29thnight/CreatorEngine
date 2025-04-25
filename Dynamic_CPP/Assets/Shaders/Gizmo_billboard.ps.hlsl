#include "Sampler.hlsli"

Texture2D gTexture : register(t0);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    float4 color = gTexture.Sample(LinearSampler, input.TexCoord);
    return color;
}
