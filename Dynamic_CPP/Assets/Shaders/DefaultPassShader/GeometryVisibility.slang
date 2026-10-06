// ABI mirrors GpuGeometryVisibility and RHIDrawIndexedIndirectArguments.
struct GeometryVisibilityCandidate
{
    float4 sphere;
    uint bin;
    uint sourceIndex;
    uint outputOffset;
    uint flags;
};

struct GeometryVisibilityBin
{
    uint indexCount;
    uint firstIndex;
    int baseVertex;
    uint preservedInstanceCount;
};

struct GeometryIndirectArguments
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint firstInstance;
};

cbuffer GeometryVisibilityConstants : register(b0)
{
    float4 geometryVisibilityPlanes[6];
    uint geometryVisibilityCandidateCount;
    uint geometryVisibilityBinCount;
    uint geometryVisibilityPlaneMask;
    uint geometryVisibilityMode;
    float4 geometryVisibilityProjectionColumns[4];
    uint2 geometryVisibilityViewSize;
    uint geometryVisibilityOcclusionLevels;
    uint geometryVisibilityProjectionValid;
    float4 geometryShadowReceiverSphere;
    float4 geometryShadowLightDirection;
};

StructuredBuffer<GeometryVisibilityCandidate> geometryVisibilityCandidates : register(t0);
StructuredBuffer<GeometryVisibilityBin> geometryVisibilityBins : register(t1);
RWStructuredBuffer<uint> geometryVisibleIds : register(u0);
RWStructuredBuffer<GeometryIndirectArguments> geometryIndirectArguments : register(u1);

#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
#include "GeometryOcclusion.slang"
#endif

bool GeometryVisibilityIndex(uint3 group, uint lane, uint count, out uint index)
{
    const uint groupIndex = group.y * 65535u + group.x;
    const uint groups = count / 64u + uint(count % 64u != 0u);
    index = 0u;
    if (groupIndex >= groups)
    {
        return false;
    }
    index = groupIndex * 64u + lane;
    return index < count;
}

bool GeometryShadowSphereVisible(float4 sphere)
{
    // Same receiver-cylinder predicate as native CPU shadows. A camera-visible
    // list or camera depth pyramid is never an input to shadow selection.
    const float3 direction = geometryShadowLightDirection.xyz;
    const float directionLengthSquared = dot(direction, direction);
    if (!all(isfinite(geometryShadowReceiverSphere)) || geometryShadowReceiverSphere.w <= 0.0f ||
        !all(isfinite(direction)) || !isfinite(directionLengthSquared) ||
        abs(directionLengthSquared - 1.0f) > 0.0001f)
    {
        return true;
    }
    const float3 offset = sphere.xyz - geometryShadowReceiverSphere.xyz;
    const float along = dot(offset, direction);
    const float3 perpendicular = offset - direction * along;
    const float perpendicularDistance = length(perpendicular);
    const float radius = geometryShadowReceiverSphere.w + sphere.w;
    // Subtraction can lose low bits at large world coordinates. Account for
    // the original magnitudes, dot/length rounding, and the light direction's
    // finite normalization error before using a boundary as a rejection proof.
    const float centerMagnitude = dot(abs(sphere.xyz) + abs(geometryShadowReceiverSphere.xyz), float3(1.0f));
    const float offsetMagnitude = dot(abs(offset), float3(1.0f));
    const float tolerance = max(0.0001f, (centerMagnitude + abs(radius)) * 0.000001f
        + offsetMagnitude * abs(directionLengthSquared - 1.0f) * 4.0f);
    const float conservativeRadius = radius + tolerance;
    if (!all(isfinite(offset)) || !isfinite(along) || !isfinite(perpendicularDistance) || !isfinite(radius) ||
        !isfinite(tolerance) || !isfinite(conservativeRadius))
    {
        return true;
    }
    return along <= conservativeRadius && perpendicularDistance <= conservativeRadius;
}

bool GeometrySphereVisible(GeometryVisibilityCandidate candidate)
{
    if ((candidate.flags & 1u) != 0u || !all(isfinite(candidate.sphere)) || candidate.sphere.w <= 0.0f)
    {
        return true;
    }
    if (geometryVisibilityMode == 1u)
    {
        return GeometryShadowSphereVisible(candidate.sphere);
    }
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
        if ((geometryVisibilityPlaneMask & (1u << planeIndex)) == 0u)
        {
            continue;
        }
        const float4 plane = geometryVisibilityPlanes[planeIndex];
        const float distance = dot(plane.xyz, candidate.sphere.xyz) + plane.w;
        const float magnitude = dot(abs(plane.xyz), abs(candidate.sphere.xyz)) + abs(plane.w) + candidate.sphere.w;
        // Keep boundary objects despite cancellation in large world coordinates.
        const float tolerance = max(0.0001f, magnitude * 0.000001f);
        if (!isfinite(distance) || !isfinite(tolerance))
        {
            return true;
        }
        if (distance < -candidate.sphere.w - tolerance)
        {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GeometryVisibilityResetCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint index;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityBinCount, index))
    {
        return;
    }
    const GeometryVisibilityBin bin = geometryVisibilityBins[index];
    GeometryIndirectArguments arguments;
    arguments.indexCount = bin.indexCount;
    arguments.instanceCount = 0u;
    arguments.firstIndex = bin.firstIndex;
    arguments.baseVertex = bin.baseVertex;
    // Vertex shaders resolve the compacted ID through a per-bin buffer slice.
    // No backend-specific base-instance semantics enter the shader contract.
    arguments.firstInstance = 0u;
    geometryIndirectArguments[index] = arguments;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GeometryVisibilityCullCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint index;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityCandidateCount, index))
    {
        return;
    }
    const GeometryVisibilityCandidate candidate = geometryVisibilityCandidates[index];
    const uint preservedCount = geometryVisibilityBins[candidate.bin].preservedInstanceCount;
    if (preservedCount != 0u)
    {
        // Write the entire identity stream, even for individually invisible
        // candidates: one survivor retains the original complete instanced draw.
        geometryVisibleIds[candidate.outputOffset + candidate.sourceIndex] = candidate.sourceIndex;
    }
    if (!GeometrySphereVisible(candidate))
    {
        return;
    }
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    if (geometryVisibilityMode == 0u && GeometrySphereOccluded(candidate.sphere, (candidate.flags & 3u) != 0u,
        geometryVisibilityProjectionColumns, geometryVisibilityViewSize,
        geometryVisibilityOcclusionLevels, geometryVisibilityProjectionValid))
    {
        return;
    }
#endif
    if (preservedCount != 0u)
    {
        InterlockedMax(geometryIndirectArguments[candidate.bin].instanceCount, preservedCount);
        return;
    }
    uint slot;
    InterlockedAdd(geometryIndirectArguments[candidate.bin].instanceCount, 1u, slot);
    geometryVisibleIds[candidate.outputOffset + slot] = candidate.sourceIndex;
}
