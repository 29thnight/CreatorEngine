#include "Includes/SsaoCommon.slang"

Texture2D<float2> gInput : register(t0);
Texture2D<float4> gNormal : register(t1);
#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> gOutput : register(u0);

float3 NormalAt(int2 pixel)
{
    const int2 fullPixel = min(int2((float2(pixel) + 0.5) * float2(gFullSize) / float2(gSize)), int2(gFullSize) - 1);
    return normalize(gNormal.Load(int3(fullPixel, 0)).xyz * 2.0 - 1.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize))
    {
        return;
    }
    const float2 center = gInput.Load(int3(id.xy, 0));
    if (center.y >= 1e4)
    {
        gOutput[id.xy] = center;
        return;
    }
    const float3 normal = NormalAt(int2(id.xy));
    float sum = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (int dy = -FILTER_RADIUS; dy <= FILTER_RADIUS; ++dy)
    {
        [unroll]
        for (int dx = -FILTER_RADIUS; dx <= FILTER_RADIUS; ++dx)
        {
            const int2 pixel = int2(id.xy) + int2(dx, dy);
            if (any(pixel < 0) || any(pixel >= int2(gSize)))
            {
                continue;
            }
            const float2 neighbour = gInput.Load(int3(pixel, 0));
            const float depthDiff = abs(neighbour.y - center.y);
            const float depthWeight = exp(-depthDiff * depthDiff / max(gDepthSigma * gDepthSigma, 1e-8));
            const float normalWeight = pow(saturate(dot(normal, NormalAt(pixel))), 32.0);
            const float spatialWeight = exp(-float(dx * dx + dy * dy) / 4.0);
            const float weight = depthWeight * normalWeight * spatialWeight;
            sum += neighbour.x * weight;
            weightSum += weight;
        }
    }
    gOutput[id.xy] = float2(weightSum > 1e-6 ? sum / weightSum : center.x, center.y);
}
