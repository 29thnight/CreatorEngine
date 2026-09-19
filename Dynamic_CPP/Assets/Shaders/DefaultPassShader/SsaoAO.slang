#include "Includes/SsaoCommon.slang"

Texture2D<float>  gDepth  : register(t0);
Texture2D<float4> gNormal : register(t1);

#ifdef __spirv__
[[vk::image_format("rg16f")]]
#endif
RWTexture2D<float2> gOutput : register(u0);

// Signed angles about the projected normal cover the entire slice hemisphere.
// Left and right occupy different bits. Test sector centers so reconstruction
// roundoff at the tangent plane cannot mark a whole sector as occluded.
uint AngleBits(float frontAngle, float backAngle)
{
    // Unwrap the short thickness arc at atan2's branch cut behind the surface.
    if (backAngle - frontAngle > kPi) backAngle -= 2.0f * kPi;
    if (frontAngle - backAngle > kPi) frontAngle -= 2.0f * kPi;
    const float loAngle = min(frontAngle, backAngle);
    const float hiAngle = max(frontAngle, backAngle);
    const int lo = int(floor(saturate(loAngle / kPi + 0.5f) * float(BITMASK_BITS) + 0.5f));
    const int hi = int(floor(saturate(hiAngle / kPi + 0.5f) * float(BITMASK_BITS) + 0.5f));
    if (hi <= lo) return 0u;
    const uint hiMask = (hi >= 32) ? 0xFFFFFFFFu : ((1u << uint(hi)) - 1u);
    const uint loMask = (lo >= 32) ? 0xFFFFFFFFu : ((1u << uint(lo)) - 1u);
    return hiMask ^ loMask;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const float2 halfUV = (float2(id.xy) + 0.5f) / float2(gSize);
    const int2 fullPixel = min(int2(halfUV * float2(gFullSize)), int2(gFullSize) - 1);
    const float2 uv = (float2(fullPixel) + 0.5f) / float2(gFullSize);
    const float depth = gDepth.Load(int3(fullPixel, 0));
    if (depth >= 1.0f)
    {
        gOutput[id.xy] = float2(1.0f, 1e4f);
        return;
    }

    const float3 viewPos = ViewFromDepth(uv, depth);
    const float3 worldNormal = gNormal.Load(int3(fullPixel, 0)).xyz * 2.0f - 1.0f;
    const float3 normal = normalize(mul(worldNormal, (float3x3)gView));
    const float3 viewDir = normalize(-viewPos);
    const float projScaleX = 1.0f / max(abs(gInverseProjection[0][0]), 1e-6f);
    const float radiusPixels = clamp(
        gRadius * projScaleX / max(abs(viewPos.z), 1e-4f) * 0.5f * float(gSize.x),
        2.0f, 128.0f);

    const float noise = Hash21(id.xy, gFrameIndex);
    float occlusion = 0.0f;
    [loop]
    for (uint d = 0; d < DIRECTIONS; ++d)
    {
        const float angle = (float(d) + noise) * kPi / float(DIRECTIONS);
        const float2 dir = float2(cos(angle), sin(angle));
        // Projection scale, aspect ratio and screen Y orientation define the slice.
        const float3 screenDir = float3(dir.x * gInverseProjection[0][0] / float(gSize.x),
            -dir.y * gInverseProjection[1][1] / float(gSize.y), 0.0f);
        const float3 sliceAxis = normalize(cross(screenDir, viewDir));
        const float3 projectedNormal = normal - sliceAxis * dot(normal, sliceAxis);
        const float projectedLength = length(projectedNormal);
        if (projectedLength < 1e-5f) continue;
        const float3 sliceNormal = projectedNormal / projectedLength;
        const float3 sliceTangent = cross(sliceNormal, sliceAxis);
        uint mask = 0u;

        [unroll]
        for (int side = 0; side < 2; ++side)
        {
            int2 previousPixel = fullPixel;
            [loop]
            for (uint s = 0; s < STEPS; ++s)
            {
                const float t = (float(s) + 1.0f + noise) / float(STEPS);
                const float2 offset = dir * (radiusPixels * t * t) * ((side == 0) ? 1.0f : -1.0f);
                const float2 sampleUV = uv + offset / float2(gSize);
                if (any(sampleUV < 0.0f) || any(sampleUV >= 1.0f)) continue;
                const int2 samplePixel = int2(sampleUV * float2(gFullSize));
                if (all(samplePixel == previousPixel)) continue;
                previousPixel = samplePixel;
                const float sampleDepth = gDepth.Load(int3(samplePixel, 0));
                if (sampleDepth >= 1.0f) continue;

                // Reconstruct the loaded texel, not the unsnapped march point.
                const float2 sampleCenter = (float2(samplePixel) + 0.5f) / float2(gFullSize);
                const float3 samplePos = ViewFromDepth(sampleCenter, sampleDepth);
                const float3 delta = samplePos - viewPos;
                const float dist = length(delta);
                if (dist < 1e-5f || dist > gRadius) continue;
                const float3 backDelta = samplePos + normalize(samplePos) * gThickness - viewPos;
                // Snapping can move the sample off the nominal slice plane.
                // Preserve its height above the actual tangent plane; projecting
                // delta onto the nominal slice first can make a flat surface
                // occlude itself. On-slice this equals dot(delta, sliceNormal).
                const float frontAngle = atan2(dot(delta, sliceTangent), dot(delta, normal) / projectedLength);
                const float backAngle = atan2(dot(backDelta, sliceTangent), dot(backDelta, normal) / projectedLength);
                mask |= AngleBits(frontAngle, backAngle);
            }
        }
        // Uniform angular visibility approximation, not cosine-weighted GTAO.
        occlusion += float(countbits(mask)) / float(BITMASK_BITS);
    }
    const float ao = saturate(1.0f - occlusion * (gIntensity / float(DIRECTIONS)));
    gOutput[id.xy] = float2(ao, viewPos.z);
}
