Texture2D<float> geometryOcclusionSource : register(t0);
#ifdef __spirv__
[[vk::image_format("r32f")]]
#endif
RWTexture2D<float> geometryOcclusionTarget : register(u0);

cbuffer GeometryOcclusionBuildConstants : register(b0)
{
    uint2 geometryOcclusionSourceSize;
    uint2 geometryOcclusionTargetSize;
};

float GeometryOcclusionSafeDepth(uint2 pixel)
{
    const float depth = geometryOcclusionSource.Load(int3(pixel, 0));
    // Unknown depth is background, not an occluder. The consumer uses standard Z.
    return isfinite(depth) && depth >= 0.0f && depth <= 1.0f ? depth : 1.0f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void GeometryOcclusionBuildCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= geometryOcclusionTargetSize))
    {
        return;
    }
    const uint2 base = id.xy * 2u;
    const uint2 last = geometryOcclusionSourceSize - 1u;
    // Ceil-sized levels cover every odd row/column, including their corner.
    // A coarser texel stores the farthest covered depth; clear pixels propagate.
    float depth = GeometryOcclusionSafeDepth(min(base, last));
    depth = max(depth, GeometryOcclusionSafeDepth(min(base + uint2(1u, 0u), last)));
    depth = max(depth, GeometryOcclusionSafeDepth(min(base + uint2(0u, 1u), last)));
    depth = max(depth, GeometryOcclusionSafeDepth(min(base + uint2(1u, 1u), last)));
    geometryOcclusionTarget[id.xy] = depth;
}
