#include "Sampler.hlsli"
Texture2D<float4> inputTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> ColorTexture : register(t2);

RWTexture2D<float4> outputTexture : register(u0);

cbuffer BiliteralFilterParams : register(b0)
{
    float2 screenSize;
    float sigmaSpace;
    float sigmaRange;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float3 centerColor = inputTexture.Load(int3(DTid.xy, 0)).rgb;
    float visibility = inputTexture.Load(int3(DTid.xy, 0)).a;
    float3 centerNormal = normalTexture.Load(int3(DTid.xy, 0)).rgb * 2.0 - 1.0;

    float sumWeight = 0.0;
    float3 result = float3(0, 0, 0);

    const int kernelRadius = 2;
    for (int x = -kernelRadius; x <= kernelRadius; ++x)
    {
        for (int y = -kernelRadius; y <= kernelRadius; ++y)
        {
            int2 offset = int2(x, y);
            int2 sampleUV = DTid.xy + offset;
            float3 sampleColor = inputTexture.Load(int3(sampleUV, 0)).rgb;
            float3 sampleNormal = normalTexture.Load(int3(sampleUV, 0)).rgb * 2.0 - 1.0;

            float dist2 = dot(offset, offset);
            float colorDiff2 = dot(centerColor - sampleColor, centerColor - sampleColor);
            float normalDiff2 = dot(centerNormal - sampleNormal, centerNormal - sampleNormal);

            float wSpatial = exp(-dist2 / (2.0 * sigmaSpace * sigmaSpace));
            float wRange = exp(-colorDiff2 / (2.0 * sigmaRange * sigmaRange));

            float weight = wSpatial * wRange;
            result += sampleColor * weight;
            sumWeight += weight;
        }
    }

    float3 output = result / sumWeight;
    
    float3 color = outputTexture.Load(int3(DTid.xy, 0)).rgb;
    
    outputTexture[DTid.xy] = float4(((color + (output.rgb)) * visibility), 1.0f);
}