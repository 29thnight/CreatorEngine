#include "Sampler.hlsli"

Texture2D<float4> inputShadingPassTexture : register(t0);
Texture2D<float4> inputLightPassTexture   : register(t1);
RWTexture2D<float4> outputTexture        : register(u0);

cbuffer BloomBuffer : register(b0)
{
    float threshHold;
    float radius; // unused here but kept for alignment/compatibility
    float2 padding;
};

[numthreads(12, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float width, height;
    inputShadingPassTexture.GetDimensions(width, height);

    float2 texelSize = float2(1.0f / width, 1.0f / height);
    float2 uvCoords = (DTid.xy + 0.5f) * texelSize;

    float3 color = inputShadingPassTexture.SampleLevel(LinearSampler, uvCoords, 0).xyz
                 + inputLightPassTexture.SampleLevel(LinearSampler, uvCoords, 0).xyz;

    if (dot(color, float3(0.2126f, 0.7152f, 0.0722f)) > threshHold)
    {
        outputTexture[DTid.xy] = float4(color, 1.0f);
    }
    else
    {
        outputTexture[DTid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
}
