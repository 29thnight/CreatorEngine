#include "Sampler.hlsli"

Texture2D<float4> SSGITexture : register(t0);
Texture2D<float4> ColorTexture : register(t1);
RWTexture2D<float4> resultTexture : register(u0);

cbuffer CompositeParams : register(b0)
{
    float2 inputTextureSize;
    int ratio;
    bool useOnlySSGI;
}

[numthreads(16,16,1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = DTid.xy / inputTextureSize / ratio;
    ////float4 SSGI = SSGITexture.SampleLevel(PointSampler, uv, 0);
    //float4 SSGI = { 0, 0, 0, 0 };
    
    //for (int x = -1; x < 2; x++)
    //{
    //    for (int y = -1; y < 2; y++)
    //    {
    //        float2 sUV = (DTid.xy + int2(x, y)) / inputTextureSize / ratio;
    //        SSGI += SSGITexture.SampleLevel(LinearSampler, sUV, 0);
    //    }
    //}
    //SSGI /= 9.0;
    
    float3 destColor = useOnlySSGI ? float3(0, 0, 0) : resultTexture.Load(int3(DTid.xy, 0)).rgb;
    
    float4 indirect = SSGITexture.SampleLevel(LinearSampler, uv, 0);
    float3 color = ColorTexture.Load(int3(DTid.xy, 0)).rgb;
    
    resultTexture[DTid.xy] = float4(((destColor + (indirect.rgb)) * indirect.a), 1.0f);
    
    //resultTexture[DTid.xy] = float4(indirect.rgb, 1.0);
}