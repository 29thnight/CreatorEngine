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
    uint geometryVisibilityPadding;
    float4 geometryVisibilityProjectionColumns[4];
    uint2 geometryVisibilityViewSize;
    uint geometryVisibilityOcclusionLevels;
    uint geometryVisibilityProjectionValid;
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

bool GeometrySphereVisible(GeometryVisibilityCandidate candidate)
{
    if ((candidate.flags & 1u) != 0u || !all(isfinite(candidate.sphere)) || candidate.sphere.w <= 0.0f)
    {
        return true;
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
    if (GeometrySphereOccluded(candidate.sphere, (candidate.flags & 3u) != 0u,
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

// Dedicated shadow compaction. Arguments use the existing 20-byte allocation;
// DispatchMeshIndirect consumes only its first 12 bytes (survivors, 1, 1).
#include "Includes/MaterialGraphSurfacePoint.slang"
StructuredBuffer<LXSurfacePoint> shadowMeshletVertices : register(t18);
#include "Includes/LxMeshletGpuContract.slang"
ByteAddressBuffer shadowMeshletTopology : register(t19);

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletResetCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint bin;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityBinCount, bin)) return;
    GeometryIndirectArguments args;
    args.indexCount = 0u; args.instanceCount = 1u; args.firstIndex = 1u;
    args.baseVertex = 0; args.firstInstance = 0u;
    geometryIndirectArguments[bin] = args;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletCullCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint meshlet;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityCandidateCount, meshlet)) return;
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(shadowMeshletTopology, meshlet);
    const uint count = descriptor.vertexCount;
    uint outside = geometryVisibilityProjectionValid != 0u ? 63u : 0u;
    for (uint vertex = 0u; vertex < count; ++vertex)
    {
        const float4 position = float4(shadowMeshletVertices[LXMeshletVertex(shadowMeshletTopology, descriptor, vertex)].position.xyz, 1);
        const float4 clip = float4(dot(position, geometryVisibilityProjectionColumns[0]),
            dot(position, geometryVisibilityProjectionColumns[1]),
            dot(position, geometryVisibilityProjectionColumns[2]), dot(position, geometryVisibilityProjectionColumns[3]));
        const float epsilon = max(1.0, max(abs(clip.w), max(abs(clip.x), max(abs(clip.y), abs(clip.z))))) * 0.00001;
        uint mask = 0u;
        if (all(isfinite(clip)))
            mask = (clip.x < -clip.w - epsilon ? 1u : 0u) | (clip.x > clip.w + epsilon ? 2u : 0u) |
                (clip.y < -clip.w - epsilon ? 4u : 0u) | (clip.y > clip.w + epsilon ? 8u : 0u) |
                (clip.z < -epsilon ? 16u : 0u) | (clip.z > clip.w + epsilon ? 32u : 0u);
        outside &= mask;
    }
    if (outside == 0u)
    {
        uint slot;
        InterlockedAdd(geometryIndirectArguments[geometryVisibilityPadding].indexCount, 1u, slot);
        // Exactly one invocation per source meshlet; capacity equals candidates.
        geometryVisibleIds[geometryVisibilityViewSize.x + slot] = meshlet;
    }
}
