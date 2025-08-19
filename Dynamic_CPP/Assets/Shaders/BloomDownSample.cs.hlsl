#include "Sampler.hlsli"

Texture2D<float4> inputTexture : register(t0);
RWTexture2D<float4> outputTexture : register(u0);

cbuffer BloomDownSampleParams : register(b0)
{
    float2 texelSize;
    uint  inputTextureMipLevel;
    uint  bloomPassIndex;
};

float luminance(float3 sourceColor)
{
    return dot(sourceColor, float3(0.299f, 0.587f, 0.114f));
}

float3 karisAverage(float3 sourceColorA, float3 sourceColorB, float3 sourceColorC, float3 sourceColorD)
{
    float A = 1.0f / (1.0f + luminance(sourceColorA));
    float B = 1.0f / (1.0f + luminance(sourceColorB));
    float C = 1.0f / (1.0f + luminance(sourceColorC));
    float D = 1.0f / (1.0f + luminance(sourceColorD));
    return (sourceColorA * A + sourceColorB * B + sourceColorC * C + sourceColorD * D) / (A + B + C + D);
}

float3 average(float3 sourceColorA, float3 sourceColorB, float3 sourceColorC, float3 sourceColorD)
{
    return (sourceColorA + sourceColorB + sourceColorC + sourceColorD) * 0.25f;
}

[numthreads(12, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uvCoords = (DTid.xy + 0.5f) * texelSize;

    float3 A = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(-2.0f, -2.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 B = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(0.0f, -2.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 C = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(2.0f, -2.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 D = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(-1.0f, -1.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 E = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(1.0f, -1.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 F = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(-2.0f, 0.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 G = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(0.0f, 0.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 H = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(2.0f, 0.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 I = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(-1.0f, 1.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 J = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(1.0f, 1.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 K = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(-2.0f, 2.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 L = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(0.0f, 2.0f) * texelSize, inputTextureMipLevel).xyz;
    float3 M = inputTexture.SampleLevel(ClampSampler, uvCoords + float2(2.0f, 2.0f) * texelSize, inputTextureMipLevel).xyz;

    if (bloomPassIndex == 0)
    {
        outputTexture[DTid.xy] = float4(karisAverage(D, E, I, J), 1.0f);
        return;
    }

    float3 centerQuad     = 0.5f   * average(D, E, I, J);
    float3 upperLeftQuad  = 0.125f * average(A, B, F, G);
    float3 upperRightQuad = 0.125f * average(B, G, C, H);
    float3 lowerLeftQuad  = 0.125f * average(F, G, K, L);
    float3 lowerRightQuad = 0.125f * average(G, L, H, M);

    outputTexture[DTid.xy] = float4(centerQuad + upperLeftQuad + upperRightQuad + lowerLeftQuad + lowerRightQuad, 1.0f);
}
