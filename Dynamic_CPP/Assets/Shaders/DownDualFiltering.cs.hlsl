#include "Sampler.hlsli"
Texture2D<float4> DownDualFilteringTexture : register(t0);
RWTexture2D<float4> targetTexture : register(u0);

cbuffer params : register(b0)
{
    float2 inputTextureSize;
};

[numthreads(16,16,1)]
void main(uint3 DTid : SV_DispathThreadID)
{
    float2 invSize = 1.0 / inputTextureSize;
    float2 uv = DTid.xy * invSize;
    float3 color = (4.0
    * DownDualFilteringTexture.SampleLevel(LinearSampler, uv, 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(1.0, 0.0) * invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(-1.0, 0.0)* invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(0.0, 1.0) * invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(0.0, -1.0)* invSize), 0).rgb) / 8.0;
    
    targetTexture[DTid.xy] = float4(color, 1);
}