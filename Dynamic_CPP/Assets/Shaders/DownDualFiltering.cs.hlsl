#include "Sampler.hlsli"
Texture2D<float4> DownDualFilteringTexture : register(t0);
RWTexture2D<float4> targetTexture : register(u0);

cbuffer params : register(b0)
{
    float2 inputTextureSize;
    int ratio;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 invSize = 1.0 / inputTextureSize;   // ex) 480, 270
    float2 uv = DTid.xy * 2 * invSize;
    float3 color = (4.0
    * DownDualFilteringTexture.SampleLevel(LinearSampler, uv, 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, ((DTid.xy * 2 + float2(1.0,  0.0)) * invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, ((DTid.xy * 2 + float2(-1.0, 0.0)) * invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, ((DTid.xy * 2 + float2(0.0,  1.0)) * invSize), 0).rgb
    + DownDualFilteringTexture.SampleLevel(LinearSampler, ((DTid.xy * 2 + float2(0.0, -1.0)) * invSize), 0).rgb) / 8.0;
    
    targetTexture[DTid.xy] = float4(color, 1);
}