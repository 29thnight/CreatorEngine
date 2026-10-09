#ifndef CREATOR_GEOMETRY_OCCLUSION_SHARED
#define CREATOR_GEOMETRY_OCCLUSION_SHARED

// Separate bindings and a constant-index switch work without descriptor indexing
// or non-uniform resource-array features on either backend.
Texture2D<float> geometryOcclusion0 : register(t2);
Texture2D<float> geometryOcclusion1 : register(t3);
Texture2D<float> geometryOcclusion2 : register(t4);
Texture2D<float> geometryOcclusion3 : register(t5);
Texture2D<float> geometryOcclusion4 : register(t6);
Texture2D<float> geometryOcclusion5 : register(t7);
Texture2D<float> geometryOcclusion6 : register(t8);
Texture2D<float> geometryOcclusion7 : register(t9);
Texture2D<float> geometryOcclusion8 : register(t10);
Texture2D<float> geometryOcclusion9 : register(t11);
Texture2D<float> geometryOcclusion10 : register(t12);
Texture2D<float> geometryOcclusion11 : register(t13);
Texture2D<float> geometryOcclusion12 : register(t14);
Texture2D<float> geometryOcclusion13 : register(t15);
Texture2D<float> geometryOcclusion14 : register(t16);
Texture2D<float> geometryOcclusion15 : register(t17);

float GeometryOcclusionLoad(uint level, uint2 pixel)
{
    const int3 address = int3(pixel, 0);
    switch (level)
    {
    case 0u: return geometryOcclusion0.Load(address);
    case 1u: return geometryOcclusion1.Load(address);
    case 2u: return geometryOcclusion2.Load(address);
    case 3u: return geometryOcclusion3.Load(address);
    case 4u: return geometryOcclusion4.Load(address);
    case 5u: return geometryOcclusion5.Load(address);
    case 6u: return geometryOcclusion6.Load(address);
    case 7u: return geometryOcclusion7.Load(address);
    case 8u: return geometryOcclusion8.Load(address);
    case 9u: return geometryOcclusion9.Load(address);
    case 10u: return geometryOcclusion10.Load(address);
    case 11u: return geometryOcclusion11.Load(address);
    case 12u: return geometryOcclusion12.Load(address);
    case 13u: return geometryOcclusion13.Load(address);
    case 14u: return geometryOcclusion14.Load(address);
    default: return geometryOcclusion15.Load(address);
    }
}

bool GeometrySphereOccluded(float4 sphere, bool skipOcclusion, float4 projectionColumns[4],
    uint2 viewSize, uint occlusionLevels, uint projectionValid)
{
    if (skipOcclusion || !all(isfinite(sphere)) || sphere.w <= 0.0f ||
        projectionValid == 0u || occlusionLevels == 0u)
    {
        return false;
    }

    float2 minimumNdc = float2(1.0f, 1.0f);
    float2 maximumNdc = float2(-1.0f, -1.0f);
    float nearestDepth = 1.0f;
    // A sphere's enclosing world AABB is conservative under arbitrary camera
    // projection. Positive homogeneous W makes each ratio attain extrema at a
    // corner. Reject the occlusion test if this box crosses the near/W plane.
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        const float3 direction = float3((corner & 1u) != 0u ? 1.0f : -1.0f,
                                        (corner & 2u) != 0u ? 1.0f : -1.0f,
                                        (corner & 4u) != 0u ? 1.0f : -1.0f);
        const float4 position = float4(sphere.xyz + direction * sphere.w, 1.0f);
        // Includes corner construction and dot-product cancellation error. An
        // ill-conditioned large-world projection therefore remains visible.
        const float4 magnitude = float4(abs(sphere.xyz) + sphere.w, 1.0f);
        float4 clip, error;
        [unroll]
        for (uint component = 0u; component < 4u; ++component)
        {
            clip[component] = dot(position, projectionColumns[component]);
            error[component] = max(0.000001f,
                dot(magnitude, abs(projectionColumns[component])) * 0.000002f);
        }
        if (!all(isfinite(clip)) || !all(isfinite(error)) ||
            clip.w - error.w <= 0.000001f || clip.z - error.z <= 0.0f)
        {
            return false;
        }
        const float lowW = clip.w - error.w;
        const float highW = clip.w + error.w;
        const float2 lowClip = clip.xy - error.xy;
        const float2 highClip = clip.xy + error.xy;
        const float2 low = min(lowClip / lowW, lowClip / highW);
        const float2 high = max(highClip / lowW, highClip / highW);
        const float depth = (clip.z - error.z) / highW;
        if (!all(isfinite(low)) || !all(isfinite(high)) || !isfinite(depth))
        {
            return false;
        }
        minimumNdc = corner == 0u ? low : min(minimumNdc, low);
        maximumNdc = corner == 0u ? high : max(maximumNdc, high);
        nearestDepth = min(nearestDepth, depth);
    }
    if (any(maximumNdc < -1.0f) || any(minimumNdc > 1.0f))
    {
        // Frustum rejection owns out-of-view geometry; uncertain occlusion does not.
        return false;
    }

    const float2 minimumUv = saturate(float2(minimumNdc.x * 0.5f + 0.5f, 0.5f - maximumNdc.y * 0.5f));
    const float2 maximumUv = saturate(float2(maximumNdc.x * 0.5f + 0.5f, 0.5f - minimumNdc.y * 0.5f));
    // Expand one full-resolution pixel before integer cell selection. Mapping
    // uses powers of two, not normalized mip dimensions: odd extents differ.
    const uint2 lastPixel = viewSize - 1u;
    const uint2 minimumPixel = uint2(max(floor(minimumUv * float2(viewSize)) - 1.0f, 0.0f));
    const uint2 maximumPixel = min(uint2(ceil(maximumUv * float2(viewSize)) + 1.0f), lastPixel);
    uint level = 0u;
    uint2 firstCell = min(minimumPixel, lastPixel) >> 1u;
    uint2 lastCell = maximumPixel >> 1u;
    while (any(lastCell - firstCell > 1u) && level + 1u < occlusionLevels)
    {
        firstCell >>= 1u;
        lastCell >>= 1u;
        ++level;
    }
    if (any(lastCell - firstCell > 1u))
    {
        return false;
    }
    // Read every covered cell (at most 2x2), never only the rectangle center.
    for (uint y = firstCell.y; y <= lastCell.y; ++y)
    {
        for (uint x = firstCell.x; x <= lastCell.x; ++x)
        {
            const float depth = GeometryOcclusionLoad(level, uint2(x, y));
            if (!isfinite(depth) || depth < 0.0f || depth >= 1.0f || nearestDepth <= depth + 0.00002f)
            {
                return false;
            }
        }
    }
    return true;
}

#endif
