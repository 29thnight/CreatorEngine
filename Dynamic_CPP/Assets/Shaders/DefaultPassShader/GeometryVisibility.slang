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
    // METEOR camera mesh bins have one owner candidate. A rejected owner must
    // never expose an uninitialized/stale ID to the compacted mesh consumer.
    if ((candidate.flags & 4u) != 0u) geometryVisibleIds[candidate.outputOffset] = 0xffffffffu;
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

// Shared camera/shadow compaction. Arguments use the 20-byte allocation;
// DispatchMeshIndirect consumes the first 12 bytes as bounded 2D dimensions.
#include "Includes/MaterialGraphSurfacePoint.slang"
StructuredBuffer<LXSurfacePoint> shadowMeshletVertices[4096] : register(t18);
#include "Includes/LxMeshletGpuContract.slang"
ByteAddressBuffer shadowMeshletTopology[4096] : register(t4114);
// Per-bin object-to-world columns (3 x float4): vertex streams are object space.
StructuredBuffer<float4> meshletWorldColumns : register(t8211);
#if defined(GEOMETRY_VISIBILITY_HISTORY)
cbuffer MeshletHistoryProjection : register(b1) { float4 meshletHistoryColumns[4]; };
#endif
#if defined(GEOMETRY_VISIBILITY_RECOVERY)
StructuredBuffer<uint> meshletFirstStage : register(t8210);
#endif

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
    geometryVisibleIds[geometryVisibilityBins[bin].firstIndex] = 0u;
}

// Each spatial group tests its own conservative original-patch sphere/error.
// Ancestor suppression selects a disjoint covering cut, identical across chunks.
bool GeometryAcceptClusterGroup(uint bin, uint base, uint group)
{
    const uint node=base+16u+group*64u;
    if (shadowMeshletTopology[bin].Load(node+8u)==0xffffffffu) return true;
    const float4 sphere=asfloat(shadowMeshletTopology[bin].Load4(node+32u));
    const float error=asfloat(shadowMeshletTopology[bin].Load(node+28u));
    const float4 metric=geometryVisibilityCandidates[bin].sphere;
    if (!all(isfinite(metric)) || metric.x<=0 || metric.y<=0 || metric.w<=0) return false;
    const float4 object = float4(sphere.xyz, 1);
    const float4 c0 = meshletWorldColumns[bin * 3u], c1 = meshletWorldColumns[bin * 3u + 1u], c2 = meshletWorldColumns[bin * 3u + 2u];
    const float scale = sqrt(dot(c0.xyz,c0.xyz)+dot(c1.xyz,c1.xyz)+dot(c2.xyz,c2.xyz)) * 1.00002;
    const float4 centerWorld = float4(dot(object,c0),dot(object,c1),dot(object,c2),1);
    const float radiusWorld = sphere.w * scale;
    float4 center, norm, rounding;
    [unroll] for (uint col=0u;col<4u;++col) {
        const float4 column = geometryVisibilityProjectionColumns[col];
        center[col] = dot(centerWorld,column);
        norm[col] = length(column.xyz) * 1.00002;
        rounding[col] = max(0.00001, dot(abs(centerWorld)+float4(radiusWorld,radiusWorld,radiusWorld,0),abs(column))*0.00002);
    }
    const float4 radius = radiusWorld * norm;
    const float lowW = center.w-radius.w-rounding.w;
    if (!all(isfinite(center+radius+rounding)) || lowW <= 0.00001 || center.z-radius.z-rounding.z <= 0) return false;
    const float maximumW = abs(center.w)+radius.w+rounding.w;
    const float2 derivative = (norm.xy*maximumW+norm.w*(abs(center.xy)+radius.xy+rounding.xy)) / (lowW*lowW) * metric.xy*0.5;
    const float pixels = length(derivative)*scale*1.00004;
    if (!isfinite(pixels) || pixels <= 0) return false;
    return error*pixels<=metric.w;
}
bool GeometryClusterSelected(uint bin, uint group)
{
    const uint base=shadowMeshletTopology[bin].Load(76);
    if (base==0u) return true; // Single original level has no replacement edges.
    const uint count=shadowMeshletTopology[bin].Load(base);
    if (group>=count || !GeometryAcceptClusterGroup(bin,base,group)) return false;
    uint parent=shadowMeshletTopology[bin].Load(base+16u+group*64u+16u);
    [loop] for(uint depth=0u;depth<8u;++depth) {
        if(parent==0xffffffffu)return true;
        if(parent>=count || GeometryAcceptClusterGroup(bin,base,parent))return false;
        parent=shadowMeshletTopology[bin].Load(base+16u+parent*64u+16u);
    }
    return false; // Cooker rejects excessive depth/cycles before GPU upload.
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletCullCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint workGroup = group.y * 65535u + group.x;
    if (workGroup >= geometryVisibilityPadding) return;
    // Upper-bound search over one CPU-sealed range per bin, never a per-meshlet
    // work list. Every lane in a group selects the same two buffer descriptors.
    uint low = 0u, high = geometryVisibilityBinCount;
    while (low + 1u < high)
    {
        const uint middle = low + (high - low) / 2u;
        if (geometryVisibilityCandidates[middle].sourceIndex <= workGroup) low = middle;
        else high = middle;
    }
    const uint bin = low;
    const uint meshlet = (workGroup - geometryVisibilityCandidates[bin].sourceIndex) * 64u + lane;
    if (meshlet >= geometryVisibilityBins[bin].indexCount) return;
    const uint classification = geometryVisibilityBins[bin].firstIndex + 1u +
        geometryVisibilityBins[bin].indexCount + meshlet;
    const bool classify = (geometryVisibilityCandidates[bin].flags & 8u) != 0u;
    if (classify) geometryVisibleIds[classification] = 0u;
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(shadowMeshletTopology[bin], meshlet);
    if (!GeometryClusterSelected(bin, descriptor.groupId)) return;
    const uint count = descriptor.vertexCount;
    uint outside = geometryVisibilityProjectionValid != 0u ? 63u : 0u;
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    float3 minimumWorld = float3(3.402823466e+38f);
    float3 maximumWorld = -minimumWorld;
    bool finiteBounds = count != 0u;
#endif
    for (uint vertex = 0u; vertex < count; ++vertex)
    {
        const float4 object = float4(shadowMeshletVertices[bin][LXMeshletVertex(shadowMeshletTopology[bin], descriptor, vertex)].position.xyz, 1);
        const float4 position = float4(dot(object, meshletWorldColumns[bin * 3u]),
            dot(object, meshletWorldColumns[bin * 3u + 1u]), dot(object, meshletWorldColumns[bin * 3u + 2u]), 1);
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
        finiteBounds = finiteBounds && all(isfinite(position));
        minimumWorld = min(minimumWorld, position.xyz);
        maximumWorld = max(maximumWorld, position.xyz);
#endif
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
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
        bool testOcclusion = true;
#if defined(GEOMETRY_VISIBILITY_RECOVERY)
        testOcclusion = meshletFirstStage[classification] == 0u;
#endif
        if (testOcclusion && finiteBounds && GeometryBoxOccluded(minimumWorld, maximumWorld,
#if defined(GEOMETRY_VISIBILITY_HISTORY)
            meshletHistoryColumns, geometryVisibilityViewSize,
#else
            geometryVisibilityProjectionColumns, geometryVisibilityViewSize,
#endif
            geometryVisibilityOcclusionLevels, geometryVisibilityProjectionValid)) return;
#endif
        if (classify) geometryVisibleIds[classification] = 1u;
        // Ordered lists keep source meshlet order; MeshletFinalizeCS compacts them.
        if ((geometryVisibilityCandidates[bin].flags & 16u) != 0u) return;
        uint slot;
        const uint outputOffset = geometryVisibilityBins[bin].firstIndex;
        InterlockedAdd(geometryVisibleIds[outputOffset], 1u, slot);
        // Exactly one invocation per source meshlet; capacity equals candidates.
        geometryVisibleIds[outputOffset + 1u + slot] = meshlet;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void MeshletFinalizeCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint bin;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityBinCount, bin)) return;
    const uint first = geometryVisibilityBins[bin].firstIndex;
    if ((geometryVisibilityCandidates[bin].flags & 16u) != 0u)
    {
        // Blended primitives must keep one deterministic order: compact the
        // per-meshlet classification in source order instead of atomic slots.
        const uint total = geometryVisibilityBins[bin].indexCount;
        uint visible = 0u;
        for (uint meshlet = 0u; meshlet < total; ++meshlet)
            if (geometryVisibleIds[first + 1u + total + meshlet] != 0u)
                geometryVisibleIds[first + 1u + visible++] = meshlet;
        geometryVisibleIds[first] = visible;
    }
    const uint count = geometryVisibleIds[first];
    geometryIndirectArguments[bin].indexCount = min(count, 65535u);
    geometryIndirectArguments[bin].instanceCount = max(1u, (count + 65534u) / 65535u);
    geometryIndirectArguments[bin].firstIndex = 1u;
}
