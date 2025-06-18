#include "Sampler.hlsli"
Texture2D<float4> UpDualFilteringTexture : register(t0);
RWTexture2D<float4> targetTexture : register(u0);

cbuffer params : register(b0)
{
    float2 inputTextureSize;
};

[numThread(16,16,1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 invSize = 1.0 / inputTextureSize;
    float2 uv = DTid.xy * invSize;
    
    float3 color = (UpDualFilteringTexture.SampleLevel(LinearSampler, uv, 0).rgb);

}