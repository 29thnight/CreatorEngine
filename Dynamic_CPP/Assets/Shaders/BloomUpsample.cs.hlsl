#include "Sampler.hlsli"

Texture2D<float4> inputPreviousUpSampleTexture : register(t0);
Texture2D<float4> inputCurrentDownSampleTexture : register(t1);
RWTexture2D<float4> outputCurrentUpSampleTexture : register(u0);

cbuffer BloomUpSampleParams : register(b0)
{
    float radius;
    uint bloomPassIndex;
    uint inputPreviousUpSampleMipLevel;
    uint maxBloomPasses;
};

[numthreads(12, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float width, height;
    outputCurrentUpSampleTexture.GetDimensions(width, height);
    float2 outputTexelSize = 1.0f / float2(width, height);

    float2 uvCoords = (DTid.xy + 0.5f) * outputTexelSize;

    // Copy current downsample level
    outputCurrentUpSampleTexture[DTid.xy] = inputCurrentDownSampleTexture.Load(int3(DTid.xy, 0));

    if (bloomPassIndex == maxBloomPasses - 1)
    {
        return;
    }

    inputPreviousUpSampleTexture.GetDimensions(width, height);
    float2 inputTexelSize = 1.0f / float2(width, height);

    float3 A = (1.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2(-1.0f, -1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 B = (2.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 0.0f, -1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 C = (1.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 1.0f, -1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 D = (2.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2(-1.0f,  0.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 E = (4.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 0.0f,  0.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 F = (2.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 1.0f,  0.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 G = (1.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2(-1.0f,  1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 H = (2.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 0.0f,  1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;
    float3 I = (1.0f / 16.0f) * inputPreviousUpSampleTexture.SampleLevel(LinearSampler, uvCoords + float2( 1.0f,  1.0f) * inputTexelSize * radius, inputPreviousUpSampleMipLevel).xyz;

    outputCurrentUpSampleTexture[DTid.xy] += float4(A + B + C + D + E + F + G + H + I, 1.0f);
}
