#include "SsaoCommon.hlsli"

Texture2D<float2> gInput : register(t0);

RWTexture2D<float2> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 center = gInput.Load(int3(id.xy, 0));
    const float centerDepth = center.y;

    float sum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int dy = -FILTER_RADIUS; dy <= FILTER_RADIUS; ++dy)
    {
        [unroll]
        for (int dx = -FILTER_RADIUS; dx <= FILTER_RADIUS; ++dx)
        {
            const int2 pixel = int2(id.xy) + int2(dx, dy);
            if (any(pixel < 0) || any(pixel >= int2(gSize))) continue;

            const float2 neighbour = gInput.Load(int3(pixel, 0));

            // 깊이가 멀면 가중치를 0으로 떨어뜨린다. 이것이 없으면 물체
            // 경계에서 배경의 AO가 물체 위로 새어 나온다.
            const float depthDiff = abs(neighbour.y - centerDepth);
            const float weight = exp(-depthDiff * depthDiff
                / max(gDepthSigma * gDepthSigma, 1e-8f));

            sum += neighbour.x * weight;
            weightSum += weight;
        }
    }

    const float filtered = (weightSum > 1e-6f) ? (sum / weightSum) : center.x;
    gOutput[id.xy] = float2(filtered, centerDepth);
}
