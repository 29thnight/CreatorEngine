#include "Sampler.hlsli"
Texture2D<float4> UpDualFilteringTexture : register(t0);
RWTexture2D<float4> targetTexture : register(u0);

cbuffer params : register(b0)
{
    float2 inputTextureSize;
    int ratio;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 invSize = 1.0 / inputTextureSize; // ex) 240, 135
    float2 uv = DTid.xy * 0.5;
    float visibility = UpDualFilteringTexture.SampleLevel(LinearSampler, uv * invSize, 0).a;
    float3 color = (      UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2( 2.0, 0.0)) * invSize, 0)
                 +        UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(-2.0, 0.0)) * invSize, 0)
                 +        UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2( 0.0, 2.0)) * invSize, 0)
                 +        UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2( 0.0,-2.0)) * invSize, 0)
                 +  2.0 * UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2( 1.0, 1.0)) * invSize, 0)
                 +  2.0 * UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(-1.0, 1.0)) * invSize, 0)
                 +  2.0 * UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2( 1.0,-1.0)) * invSize, 0)
                 +  2.0 * UpDualFilteringTexture.SampleLevel(LinearSampler, (uv + float2(-1.0,-1.0)) * invSize, 0)) / 12.0;
    
    float3 originColor = UpDualFilteringTexture.SampleLevel(LinearSampler, uv * invSize, 0).rgb;
    float3 temp = lerp(originColor, color, smoothstep(originColor, color, 0.25));
    
    targetTexture[DTid.xy] = float4(color, visibility);
}