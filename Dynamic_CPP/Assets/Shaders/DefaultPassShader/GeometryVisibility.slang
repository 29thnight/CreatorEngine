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
    uint padding;
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
};

StructuredBuffer<GeometryVisibilityCandidate> geometryVisibilityCandidates : register(t0);
StructuredBuffer<GeometryVisibilityBin> geometryVisibilityBins : register(t1);
RWStructuredBuffer<uint> geometryVisibleIds : register(u0);
RWStructuredBuffer<GeometryIndirectArguments> geometryIndirectArguments : register(u1);

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
    if (!GeometrySphereVisible(candidate))
    {
        return;
    }
    uint slot;
    InterlockedAdd(geometryIndirectArguments[candidate.bin].instanceCount, 1u, slot);
    geometryVisibleIds[candidate.outputOffset + slot] = candidate.sourceIndex;
}
