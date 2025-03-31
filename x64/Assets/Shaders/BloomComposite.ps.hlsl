#include "Sampler.hlsli"

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);

cbuffer CompositeParams : register(b0)
{
    float coefficient;
}

struct FullscreenVsOutput
{
    float4 position : SV_POSITION; // vertex position
    float2 texCoord : TEXCOORD0;
};

// pixel shader
float4 PSMain(FullscreenVsOutput p) : SV_TARGET
{
    // output: tex0 + coefficient * tex1
    return mad(coefficient, tex1.Sample(LinearSampler, p.texCoord), tex0.Sample(LinearSampler, p.texCoord));
}