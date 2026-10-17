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
#include "Includes/MeshletGeometryAccess.slang"
StructuredBuffer<float4> shadowMeshletVertices[4096] : register(t18);
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

struct MeshletViewData
{
    float4 projection[4];
    float4 history[4];
    uint width, height, levels, valid;
    uint mode, bins, outputWords, reserved;
};
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
StructuredBuffer<MeshletViewData> meshletViews : register(t8212);
RWStructuredBuffer<uint> meshletViewIds[8] : register(u2);
RWStructuredBuffer<GeometryIndirectArguments> meshletViewArgs[8] : register(u10);
StructuredBuffer<uint> meshletViewFirst[8] : register(t8213);
#define MESHLET_IDS(index) meshletViewIds[NonUniformResourceIndex(geometryVisibilityCandidates[index].bin)]
#define MESHLET_ARGS(index) meshletViewArgs[NonUniformResourceIndex(geometryVisibilityCandidates[index].bin)][geometryVisibilityCandidates[index].outputOffset]
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
Texture2D<float> meshletViewHzb[128] : register(t16384);
#endif
#else
#define MESHLET_IDS(bin) geometryVisibleIds
#define MESHLET_ARGS(bin) geometryIndirectArguments[bin]
#endif
MeshletViewData MeshletView(uint bin)
{
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    return meshletViews[geometryVisibilityCandidates[bin].bin];
#else
    MeshletViewData view;
    [unroll] for (uint c = 0u; c < 4u; ++c)
    {
        view.projection[c] = geometryVisibilityProjectionColumns[c];
#if defined(GEOMETRY_VISIBILITY_HISTORY)
        view.history[c] = meshletHistoryColumns[c];
#else
        view.history[c] = view.projection[c];
#endif
    }
    view.width = geometryVisibilityViewSize.x; view.height = geometryVisibilityViewSize.y;
    view.levels = geometryVisibilityOcclusionLevels; view.valid = geometryVisibilityProjectionValid;
    view.mode = 0u; view.bins = geometryVisibilityBinCount; view.outputWords = 0u; view.reserved = 0u;
    return view;
#endif
}
uint MeshletJobBase(uint bin)
{
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    return geometryVisibilityCandidates[bin].sourceIndex;
#else
    return geometryVisibilityBins[bin].firstIndex;
#endif
}
// Meshlet-only bit in planeMask; instance visibility retains its six plane bits.
// A view/stage owns a bounded ring. Every resident wave both scatters and gathers;
// no dedicated producer can block consumers from being scheduled.
bool MeshletUsesWorkQueue()
{
    return (geometryVisibilityPlaneMask & 0x80000000u) != 0u;
}
uint MeshletWorkQueueBase()
{
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    return 0u;
#else
    const GeometryVisibilityBin last = geometryVisibilityBins[geometryVisibilityBinCount - 1u];
    const bool feedback = (geometryVisibilityCandidates[geometryVisibilityBinCount - 1u].flags & 32u) != 0u;
    return last.firstIndex + last.indexCount * (feedback ? 3u : 2u) + 1u;
#endif
}
// Header: published meshlets, completed meshlets, faults, working waves, head, tail,
// initial partition cursor, full retries, empty reads, capacity, nonempty partitions,
// active wave width; JOB_COUNT, producer leases, consumer leases, reserved.
// Slots carry a nonzero meshlet job; zero is empty. Completion is queue-driven.
// Publication and retirement are single-word atomics: there is no separate
// payload whose visibility would depend on an inter-wave memory fence.
uint MeshletRingCapacity()
{
#if defined(GEOMETRY_VISIBILITY_RING_STRESS)
    return 1u;
#else
    return 256u;
#endif
}
uint MeshletClaimWork(uint queue)
{
    uint ticket;
    InterlockedAdd(geometryVisibleIds[queue + 6u], 0u, ticket);
    [loop] while (ticket < geometryVisibilityBinCount * 16u)
    {
        uint observed;
        InterlockedCompareExchange(geometryVisibleIds[queue + 6u], ticket, ticket + 1u, observed);
        if (observed == ticket)
        {
            return ticket;
        }
        ticket = observed;
    }
    return 0xffffffffu;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletResetCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint bin;
    if (!GeometryVisibilityIndex(group, lane, geometryVisibilityBinCount, bin)) return;
    GeometryIndirectArguments args;
    args.indexCount = 0u; args.instanceCount = 1u; args.firstIndex = 1u;
    args.baseVertex = 0; args.firstInstance = 0u;
    MESHLET_ARGS(bin) = args;
    MESHLET_IDS(bin)[geometryVisibilityBins[bin].firstIndex] = 0u;
    if (MeshletUsesWorkQueue())
    {
        const uint queue = MeshletWorkQueueBase();
        if (bin == 0u)
        {
            [unroll] for (uint word = 0u; word < 16u; ++word)
            {
                geometryVisibleIds[queue + word] = 0u;
            }
            geometryVisibleIds[queue + 9u] = MeshletRingCapacity();
            [loop] for (uint slot = 0u; slot < MeshletRingCapacity(); ++slot)
            {
                geometryVisibleIds[queue + 16u + slot] = 0u;
            }
        }
    }
}

// Each spatial group tests its own conservative original-patch sphere/error.
// Ancestor suppression selects a disjoint covering cut, identical across chunks.
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS) && defined(GEOMETRY_VISIBILITY_OCCLUSION)
bool GeometrySharedBoxOccluded(float3 minimumWorld, float3 maximumWorld, float4 projectionColumns[4],
    uint2 viewSize, uint occlusionLevels, uint projectionValid, uint viewId)
{
    if (!all(isfinite(minimumWorld)) || !all(isfinite(maximumWorld)) || any(minimumWorld > maximumWorld) ||
        any(viewSize == 0u) || projectionValid == 0u || occlusionLevels == 0u)
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
        const float4 position = float4((corner & 1u) != 0u ? maximumWorld.x : minimumWorld.x,
            (corner & 2u) != 0u ? maximumWorld.y : minimumWorld.y,
            (corner & 4u) != 0u ? maximumWorld.z : minimumWorld.z, 1.0f);
        // Includes corner construction and dot-product cancellation error. An
        // ill-conditioned large-world projection therefore remains visible.
        const float4 magnitude = float4(max(abs(minimumWorld), abs(maximumWorld)), 1.0f);
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
            const float depth = meshletViewHzb[NonUniformResourceIndex(viewId * 16u + level)].Load(int3(x, y, 0));
            if (!isfinite(depth) || depth < 0.0f || depth >= 1.0f || nearestDepth <= depth + 0.00002f)
            {
                return false;
            }
        }
    }
    return true;
}

#endif
bool GeometryAcceptClusterGroup(uint bin, uint base, uint group, bool demand)
{
    const MeshletViewData view = MeshletView(bin);
    if (!demand && !MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)],group))
    {
        return false;
    }
    const uint node=base+16u+group*64u;
    const uint child=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node+8u);
    const uint second=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node+12u);
    if (!demand && child!=0xffffffffu && (!MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)],child) ||
        !MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)],second)))
    {
        return true;
    }
    if (shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node+8u)==0xffffffffu) return true;
    const float4 sphere=asfloat(shadowMeshletTopology[NonUniformResourceIndex(bin)].Load4(node+32u));
    const float error=asfloat(shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node+28u));
    const float4 metric=geometryVisibilityCandidates[bin].sphere;
    if (!all(isfinite(metric)) || metric.x<=0 || metric.y<=0 || metric.w<=0) return false;
    const float4 object = float4(sphere.xyz, 1);
    const float4 c0 = meshletWorldColumns[bin * 3u], c1 = meshletWorldColumns[bin * 3u + 1u], c2 = meshletWorldColumns[bin * 3u + 2u];
    const float scale = sqrt(dot(c0.xyz,c0.xyz)+dot(c1.xyz,c1.xyz)+dot(c2.xyz,c2.xyz)) * 1.00002;
    const float4 centerWorld = float4(dot(object,c0),dot(object,c1),dot(object,c2),1);
    const float radiusWorld = sphere.w * scale;
    float4 center, norm, rounding;
    [unroll] for (uint col=0u;col<4u;++col) {
        const float4 column = view.projection[col];
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
bool GeometryClusterSelected(uint bin, uint group, bool demand)
{
    const uint base=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(76);
    if (base==0u) return true; // Single original level has no replacement edges.
    const uint count=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base);
    if (group>=count || !GeometryAcceptClusterGroup(bin,base,group,demand)) return false;
    uint parent=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base+16u+group*64u+16u);
    [loop] for(uint depth=0u;depth<8u;++depth) {
        if(parent==0xffffffffu)return true;
        if(parent>=count || GeometryAcceptClusterGroup(bin,base,parent,demand))return false;
        parent=shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base+16u+parent*64u+16u);
    }
    return false; // Cooker rejects excessive depth/cycles before GPU upload.
}

void GeometryCullMeshlet(uint bin, uint meshlet)
{
    const MeshletViewData view = MeshletView(bin);
    if (meshlet >= geometryVisibilityBins[bin].indexCount) return;
    const uint classification = geometryVisibilityBins[bin].firstIndex + 1u +
        geometryVisibilityBins[bin].indexCount + meshlet;
    const bool classify = (geometryVisibilityCandidates[bin].flags & 8u) != 0u;
    if (classify) MESHLET_IDS(bin)[classification] = 0u;
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(shadowMeshletTopology[NonUniformResourceIndex(bin)], meshlet);
    if ((geometryVisibilityCandidates[bin].flags & 32u) != 0u)
    {
        // Demand uses the desired GPU LOD cut independently of current residency.
        // The conservative request includes offscreen/skinned clusters; raster
        // selection below still uses the complete resident ancestor fallback.
        const uint feedback = geometryVisibilityBins[bin].firstIndex + 1u +
            geometryVisibilityBins[bin].indexCount * 2u + meshlet;
        MESHLET_IDS(bin)[feedback] = GeometryClusterSelected(bin, descriptor.groupId, true) ? 1u : 0u;
    }
    if (!MeteorClusterResident(shadowMeshletTopology[NonUniformResourceIndex(bin)],meshlet))
    {
        return;
    }
    if (!GeometryClusterSelected(bin, descriptor.groupId, false)) return;
    const uint count = descriptor.vertexCount;
    uint outside = view.valid != 0u ? 63u : 0u;
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    float3 minimumWorld = float3(3.402823466e+38f);
    float3 maximumWorld = -minimumWorld;
    bool finiteBounds = count != 0u;
#endif
    for (uint vertex = 0u; vertex < count; ++vertex)
    {
        const float4 object = float4(MeteorGeometryPoint(shadowMeshletVertices[NonUniformResourceIndex(bin)],shadowMeshletTopology[NonUniformResourceIndex(bin)],LXMeshletVertex(shadowMeshletTopology[NonUniformResourceIndex(bin)],descriptor,vertex)).position.xyz, 1);
        const float4 position = float4(dot(object, meshletWorldColumns[bin * 3u]),
            dot(object, meshletWorldColumns[bin * 3u + 1u]), dot(object, meshletWorldColumns[bin * 3u + 2u]), 1);
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
        finiteBounds = finiteBounds && all(isfinite(position));
        minimumWorld = min(minimumWorld, position.xyz);
        maximumWorld = max(maximumWorld, position.xyz);
#endif
        const float4 clip = float4(dot(position, view.projection[0]),
            dot(position, view.projection[1]),
            dot(position, view.projection[2]), dot(position, view.projection[3]));
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
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
        const uint viewId = geometryVisibilityCandidates[bin].bin;
        bool testOcclusion = view.mode != 0u;
        [branch] if (view.mode == 3u)
        {
            testOcclusion = meshletViewFirst[NonUniformResourceIndex(viewId)][classification] == 0u;
        }
        float4 occlusionProjection[4];
        [unroll] for (uint column = 0u; column < 4u; ++column)
        {
            occlusionProjection[column] = view.mode == 2u ? view.history[column] : view.projection[column];
        }
        if (testOcclusion && finiteBounds && GeometrySharedBoxOccluded(minimumWorld, maximumWorld,
            occlusionProjection, uint2(view.width, view.height), view.levels, view.valid, viewId))
        {
            return;
        }
#else
        bool testOcclusion = true;
#if defined(GEOMETRY_VISIBILITY_RECOVERY)
        testOcclusion = meshletFirstStage[classification] == 0u;
#endif
        if (testOcclusion && finiteBounds && GeometryBoxOccluded(minimumWorld, maximumWorld,
#if defined(GEOMETRY_VISIBILITY_HISTORY)
            meshletHistoryColumns, uint2(view.width, view.height),
#else
            view.projection, uint2(view.width, view.height),
#endif
            view.levels, view.valid)) return;
#endif
#endif
        if (classify) MESHLET_IDS(bin)[classification] = 1u;
        // Ordered lists keep source meshlet order; MeshletFinalizeCS compacts them.
        if ((geometryVisibilityCandidates[bin].flags & 16u) != 0u) return;
        uint slot;
        const uint outputOffset = geometryVisibilityBins[bin].firstIndex;
        InterlockedAdd(MESHLET_IDS(bin)[outputOffset], 1u, slot);
        // Exactly one invocation per source meshlet; capacity equals candidates.
        MESHLET_IDS(bin)[outputOffset + 1u + slot] = meshlet;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletCullCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (!MeshletUsesWorkQueue())
    {
        // Fixed-range oracle for regression comparison; production uses the queue.
        const uint workGroup = group.y * 65535u + group.x;
        if (workGroup >= geometryVisibilityPadding)
        {
            return;
        }
        uint low = 0u, high = geometryVisibilityBinCount;
        while (low + 1u < high)
        {
            const uint middle = low + (high - low) / 2u;
            if (geometryVisibilityCandidates[middle].sourceIndex <= workGroup)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        GeometryCullMeshlet(low, (workGroup - geometryVisibilityCandidates[low].sourceIndex) * 64u + lane);
        return;
    }
#if defined(GEOMETRY_VISIBILITY_QUEUE_PARTIAL_WAVE)
    // Regression-only mask: production must also handle partially active waves.
    if ((WaveGetLaneIndex() & 1u) != 0u)
    {
        return;
    }
#endif
    // Dense active-lane rank also covers a partially occupied hardware wave.
    const uint workerLane = WavePrefixCountBits(true);
    const uint workerWidth = WaveActiveCountBits(true);
    const uint queue = MeshletWorkQueueBase();
    if (WaveIsFirstLane())
    {
        uint observed;
        InterlockedCompareExchange(geometryVisibleIds[queue + 11u], 0u, workerWidth, observed);
        if (observed != 0u && observed != workerWidth)
        {
            InterlockedOr(geometryVisibleIds[queue + 2u], 2u);
        }
    }
    bool hadWork = false;
    uint pending = 0u; // Each lane retains its own unpublished meshlet.
    uint sourceBin = 0u;
    uint sourceOffset = 0u;
    uint sourceRemaining = 0u;
    uint iteration = 0u;
    bool producerLease = false;
    [loop] while (true)
    {
        uint completed = 0u;
        uint partitions = 0u;
        if (WaveIsFirstLane())
        {
            InterlockedAdd(geometryVisibleIds[queue + 1u], 0u, completed);
            InterlockedAdd(geometryVisibleIds[queue + 6u], 0u, partitions);
        }
        completed = WaveReadLaneFirst(completed);
        partitions = WaveReadLaneFirst(partitions);
        uint queued = 1u, producers = 1u;
        if (WaveIsFirstLane())
        {
            InterlockedAdd(geometryVisibleIds[queue + 12u], 0u, queued);
            InterlockedAdd(geometryVisibleIds[queue + 13u], 0u, producers);
        }
        queued = WaveReadLaneFirst(queued);
        producers = WaveReadLaneFirst(producers);
        if (!producerLease && sourceRemaining == 0u && !WaveActiveAnyTrue(pending != 0u) &&
            partitions == geometryVisibilityBinCount * 16u && queued == 0u && producers == 0u)
        {
            // A gathered job finishes in this wave before the next iteration.
            // Other consumers finish their own jobs before exiting as well.
            // Waiting for all consumers to be simultaneously idle can livelock.
            break;
        }
#if defined(GEOMETRY_VISIBILITY_RING_STRESS)
        if (iteration >= 4u)
#endif
        {
            // A wave acquires one of sixteen logical subdivisions per bin.
            // Use the published RE subdivision formula, including small inputs
            // and the final subdivision's integer-division remainder.
            if (partitions < geometryVisibilityBinCount * 16u &&
                !WaveActiveAnyTrue(pending != 0u) && sourceRemaining == 0u)
            {
                uint partition = 0xffffffffu;
                if (WaveIsFirstLane())
                {
                    InterlockedAdd(geometryVisibleIds[queue + 13u], 1u);
                    partition = MeshletClaimWork(queue);
                }
                partition = WaveReadLaneFirst(partition);
                producerLease = partition != 0xffffffffu;
                if (!producerLease && WaveIsFirstLane())
                {
                    InterlockedAdd(geometryVisibleIds[queue + 13u], 0xffffffffu);
                }
                if (partition != 0xffffffffu)
                {
                    sourceBin = partition / 16u;
                    const uint subdivision = partition & 15u;
                    const uint meshlets = geometryVisibilityBins[sourceBin].indexCount;
                    sourceRemaining = meshlets / 16u;
                    sourceOffset = sourceRemaining * subdivision;
                    if (subdivision == 15u)
                    {
                        sourceRemaining = meshlets - sourceOffset;
                    }
                    if (meshlets < workerWidth * 16u)
                    {
                        sourceOffset = workerWidth * subdivision;
                        sourceRemaining = min(meshlets > sourceOffset ? meshlets - sourceOffset : 0u, workerWidth);
                    }
                    if (sourceRemaining != 0u && WaveIsFirstLane())
                    {
                        InterlockedAdd(geometryVisibleIds[queue + 10u], 1u);
                    }
                }
            }
            if (!WaveActiveAnyTrue(pending != 0u) && sourceRemaining != 0u)
            {
                const uint batch = min(sourceRemaining, workerWidth);
                if (workerLane < batch)
                {
                    pending = MeshletJobBase(sourceBin) + 1u + sourceOffset + workerLane;
                }
                sourceOffset += batch;
                sourceRemaining -= batch;
            }
            // Only publishing lanes participate. Reserve their slots together,
            // then each lane publishes a nonzero 32-bit job with its own CAS.
            if (pending != 0u)
            {
                const uint count = WaveActiveCountBits(true);
                const uint rank = WavePrefixCountBits(true);
                uint tail = 0u;
                if (WaveIsFirstLane())
                {
                    InterlockedAdd(geometryVisibleIds[queue + 5u], count, tail);
                }
                tail = WaveReadLaneFirst(tail) + rank;
                uint observed;
                InterlockedCompareExchange(geometryVisibleIds[queue + 16u + (tail & (MeshletRingCapacity() - 1u))],
                    0u, pending, observed);
                const uint published = WaveActiveCountBits(observed == 0u);
                if (WaveIsFirstLane())
                {
                    InterlockedAdd(geometryVisibleIds[queue], published);
                    InterlockedAdd(geometryVisibleIds[queue + 12u], published);
                    InterlockedAdd(geometryVisibleIds[queue + 7u], count - published);
                }
                if (observed == 0u)
                {
                    pending = 0u;
                }
            }
        }
        if (producerLease && sourceRemaining == 0u && !WaveActiveAnyTrue(pending != 0u))
        {
            if (WaveIsFirstLane())
            {
                InterlockedAdd(geometryVisibleIds[queue + 13u], 0xffffffffu);
            }
            producerLease = false;
        }
        if (WaveIsFirstLane())
        {
            InterlockedAdd(geometryVisibleIds[queue + 14u], workerWidth);
        }
        uint job = 0u;
#if defined(GEOMETRY_VISIBILITY_RING_STRESS)
        if (iteration < 4u || iteration >= 12u)
#endif
        {
            uint head = 0u;
            if (WaveIsFirstLane())
            {
                InterlockedAdd(geometryVisibleIds[queue + 4u], workerWidth, head);
            }
            head = WaveReadLaneFirst(head) + workerLane;
            InterlockedMin(geometryVisibleIds[queue + 16u + (head & (MeshletRingCapacity() - 1u))], 0u, job);
            const uint empty = WaveActiveCountBits(job == 0u);
            if (WaveIsFirstLane())
            {
                InterlockedAdd(geometryVisibleIds[queue + 8u], empty);
            }
        }
        const uint gathered = WaveActiveCountBits(job != 0u);
        if (WaveIsFirstLane())
        {
            InterlockedAdd(geometryVisibleIds[queue + 12u], 0u - gathered);
        }
        ++iteration;
        if (job != 0u)
        {
            hadWork = true;
            const uint address = job - 1u;
            uint low = 0u, high = geometryVisibilityBinCount;
            [loop] while (low + 1u < high)
            {
                const uint middle = low + (high - low) / 2u;
                if (MeshletJobBase(middle) <= address)
                {
                    low = middle;
                }
                else
                {
                    high = middle;
                }
            }
            const uint meshlet = address - MeshletJobBase(low);
            if (meshlet >= geometryVisibilityBins[low].indexCount)
            {
                InterlockedOr(geometryVisibleIds[queue + 2u], 1u);
            }
            else
            {
                // Gathered lanes can select different geometry descriptors.
                GeometryCullMeshlet(low, meshlet);
            }
        }
        const uint finished = WaveActiveCountBits(job != 0u);
        if (WaveIsFirstLane())
        {
            InterlockedAdd(geometryVisibleIds[queue + 1u], finished);
            InterlockedAdd(geometryVisibleIds[queue + 14u], 0u - workerWidth);
        }
    }
    if (WaveActiveAnyTrue(hadWork) && WaveIsFirstLane())
    {
        InterlockedAdd(geometryVisibleIds[queue + 3u], 1u);
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
            if (MESHLET_IDS(bin)[first + 1u + total + meshlet] != 0u)
                MESHLET_IDS(bin)[first + 1u + visible++] = meshlet;
        MESHLET_IDS(bin)[first] = visible;
    }
    const uint count = MESHLET_IDS(bin)[first];
    MESHLET_ARGS(bin).indexCount = min(count, 65535u);
    MESHLET_ARGS(bin).instanceCount = max(1u, (count + 65534u) / 65535u);
    MESHLET_ARGS(bin).firstIndex = 1u;
}
