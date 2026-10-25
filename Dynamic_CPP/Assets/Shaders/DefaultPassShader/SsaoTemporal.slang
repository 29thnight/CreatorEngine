Texture2D<float2> gCurrent : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float> gDepth : register(t2);
Texture2D<float4> gHistory : register(t3);
Texture2D<float4> gGeometryHistory : register(t4);
Texture2D<float2> gMotion : register(t5);
Texture2D<float> gResponsive : register(t6);
Texture2D<float> gReactive : register(t7);

#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> gResolved : register(u0);
#ifdef __spirv__
[[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> gNextHistory : register(u1);
#ifdef __spirv__
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> gNextGeometry : register(u2);

cbuffer TemporalParams : register(b0)
{
    float4x4 gInverseProjection;
    float4x4 gInverseView;
    float4x4 gPreviousViewProjection;
    float4x4 gPreviousView;
    uint2 gSize;
    uint2 gFullSize;
    uint gHasHistory;
    uint gHasMotion;
    uint gMaxFrames;
    uint gPadding;
    float gDepthTolerance;
    float gNormalThreshold;
    float2 gJitterDelta;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize))
    {
        return;
    }
    const float2 ao = gCurrent.Load(int3(id.xy, 0));
    const int2 pixel = min(int2((float2(id.xy) + 0.5) * float2(gFullSize) / float2(gSize)), int2(gFullSize) - 1);
    const float2 uv = (float2(pixel) + 0.5) / float2(gFullSize);
    const float depth = gDepth.Load(int3(pixel, 0));
    const float3 normal = normalize(gNormal.Load(int3(pixel, 0)).xyz * 2.0 - 1.0);
    float visibility = saturate(ao.x);
    float frames = 1.0;
    float4 view = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0), gInverseProjection);
    view /= view.w;
    const float4 world = mul(float4(view.xyz, 1.0), gInverseView);
    float4 previousClip = mul(world, gPreviousViewProjection);
    const float expectedDepth = mul(world, gPreviousView).z;
    bool valid = gHasHistory != 0 && depth < 1.0 && previousClip.w > 1e-5 && expectedDepth > 0.0;
    float2 previousUv = float2(previousClip.x / previousClip.w * 0.5 + 0.5, 0.5 - previousClip.y / previousClip.w * 0.5);
    if (gHasMotion != 0)
    {
        const float2 motion = gMotion.Load(int3(pixel, 0));
        valid = valid && all(isfinite(motion)) &&
            max(gResponsive.Load(int3(pixel, 0)), gReactive.Load(int3(pixel, 0))) < 0.5;
        previousUv = uv + (motion + gJitterDelta) / float2(gFullSize);
    }
    valid = valid && all(isfinite(previousUv)) && all(previousUv >= 0.0) && all(previousUv < 1.0);
    if (valid)
    {
        // Validate each bilinear tap independently; an edge tap may belong to a
        // different surface even when the reprojected center has valid depth.
        const float2 coord = previousUv * float2(gSize) - 0.5;
        const int2 base = int2(floor(coord));
        const float2 fraction = frac(coord);
        float weightSum = 0.0;
        float historySum = 0.0;
        float frameSum = 0.0;
        [unroll]
        for (int y = 0; y < 2; ++y)
        {
            [unroll]
            for (int x = 0; x < 2; ++x)
            {
                const int2 samplePixel = base + int2(x, y);
                if (any(samplePixel < 0) || any(samplePixel >= int2(gSize)))
                {
                    continue;
                }
                const float4 geometry = gGeometryHistory.Load(int3(samplePixel, 0));
                const float4 history = gHistory.Load(int3(samplePixel, 0));
                const float tolerance = max(gDepthTolerance, abs(expectedDepth) * 0.005);
                if (!all(isfinite(geometry)) || !all(isfinite(history)) || history.y < 1.0 ||
                    abs(geometry.w - expectedDepth) > tolerance || dot(geometry.xyz, normal) < gNormalThreshold)
                {
                    continue;
                }
                const float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                    (y == 0 ? 1.0 - fraction.y : fraction.y);
                weightSum += weight;
                historySum += history.x * weight;
                frameSum += history.y * weight;
            }
        }
        if (weightSum > 1e-4)
        {
            // Clamp only to samples on this surface. A moving occluder can change
            // AO while the receiver itself retains identical depth and normals.
            float lo = visibility;
            float hi = visibility;
            float sum = 0.0;
            float squareSum = 0.0;
            float count = 0.0;
            [unroll]
            for (int y = -1; y <= 1; ++y)
            {
                [unroll]
                for (int x = -1; x <= 1; ++x)
                {
                    const int2 q = clamp(int2(id.xy) + int2(x, y), int2(0, 0), int2(gSize) - 1);
                    const float2 sample = gCurrent.Load(int3(q, 0));
                    const int2 fullQ = min(int2((float2(q) + 0.5) * float2(gFullSize) / float2(gSize)), int2(gFullSize) - 1);
                    const float3 sampleNormal = normalize(gNormal.Load(int3(fullQ, 0)).xyz * 2.0 - 1.0);
                    if (abs(sample.y - ao.y) <= max(gDepthTolerance, abs(ao.y) * 0.01) &&
                        dot(sampleNormal, normal) >= gNormalThreshold)
                    {
                        lo = min(lo, sample.x);
                        hi = max(hi, sample.x);
                        sum += sample.x;
                        squareSum += sample.x * sample.x;
                        count += 1.0;
                    }
                }
            }
            const float mean = sum / max(count, 1.0);
            const float deviation = sqrt(max(squareSum / max(count, 1.0) - mean * mean, 0.0));
            float history = clamp(historySum / weightSum, max(lo, mean - 2.0 * deviation - 0.02),
                min(hi, mean + 2.0 * deviation + 0.02));
            frames = min(frameSum / weightSum + 1.0, float(max(gMaxFrames, 1u)));
            const float motionPixels = length((previousUv - uv) * float2(gFullSize));
            if (abs(history - visibility) > 0.2 || motionPixels > 1.0)
            {
                frames = min(frames, 4.0);
            }
            visibility = lerp(history, visibility, 1.0 / frames);
        }
    }
    gResolved[id.xy] = float2(visibility, ao.y);
    // Store the unfiltered resolve so spatial blur is never fed back repeatedly.
    gNextHistory[id.xy] = float4(visibility, frames, 0.0, 0.0);
    gNextGeometry[id.xy] = depth < 1.0 ? float4(normal, view.z) : float4(0.0, 0.0, 0.0, 0.0);
}
