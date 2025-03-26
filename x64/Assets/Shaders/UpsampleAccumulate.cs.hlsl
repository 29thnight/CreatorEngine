// UpsampleAndAccumulateCS.hlsl
#include "Sampler.hlsli"

cbuffer UpsampleCB : register(b0)
{
    float blendFactor;
    uint2 outputSize;
};

Texture2D<float4> LowResTex : register(t0); // lower resolution (blurred)
Texture2D<float4> HighResTex : register(t1); // current resolution (accumulation target)
RWTexture2D<float4> Output : register(u0);



[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputSize.x || DTid.y >= outputSize.y)
        return;

    float2 uv = float2(DTid.xy) / outputSize;

    float4 low = LowResTex.SampleLevel(LinearSampler, uv, 0);
    float4 high = HighResTex.Load(int3(DTid.xy, 0));

    Output[DTid.xy] = saturate(high + low * blendFactor);
}
