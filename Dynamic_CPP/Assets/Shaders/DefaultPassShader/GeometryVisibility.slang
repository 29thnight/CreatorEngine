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
#include "Includes/GeometryInstanceBounds.slang"
StructuredBuffer<float4> shadowMeshletVertices[4096] : register(t18);
#include "Includes/LxMeshletGpuContract.slang"
ByteAddressBuffer shadowMeshletTopology[4096] : register(t4114);
// 경계 ABI를 쓰는 bin은 world 3행과 불변 local 경계 2행을 함께 업로드한다.
StructuredBuffer<float4> meshletWorldColumns : register(t8211);
#if defined(GEOMETRY_VISIBILITY_HISTORY) || defined(GEOMETRY_VISIBILITY_CAMERA_PREDICTION)
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
#if defined(GEOMETRY_VISIBILITY_HISTORY) || defined(GEOMETRY_VISIBILITY_CAMERA_PREDICTION)
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
bool MeshletHasGroups(uint bin)
{
    return (geometryVisibilityCandidates[bin].flags & 64u) != 0u;
}
bool MeshletHasInstanceBounds(uint bin)
{
    return (geometryVisibilityCandidates[bin].flags & 512u) != 0u;
}
uint MeshletWorldBase(uint bin)
{
    return bin * (MeshletHasInstanceBounds(bin) ? 5u : 3u);
}
uint MeshletGroupOwner(uint bin) { return uint(geometryVisibilityCandidates[bin].sphere.z); }
uint MeshletGroupCount(uint bin) { return uint(geometryVisibilityBins[bin].baseVertex); }
uint MeshletGroupMaskWords(uint bin) { return (MeshletGroupCount(bin) + 15u) / 16u; }
uint MeshletGroupMasks(uint bin)
{
    bin = MeshletGroupOwner(bin);
    return geometryVisibilityBins[bin].firstIndex + 1u + geometryVisibilityBins[bin].indexCount * 2u;
}
uint MeshletFeedbackWords(uint bin)
{
    return (geometryVisibilityCandidates[bin].flags & 32u) != 0u ? (MeshletGroupCount(bin) + 31u) / 32u : 0u;
}
uint MeshletFeedbackOffset(uint bin)
{
    bin = MeshletGroupOwner(bin);
    return MeshletGroupMasks(bin) + MeshletGroupMaskWords(bin) * 2u;
}
uint MeshletInstanceOffset(uint bin)
{
    const uint local = geometryVisibilityBins[bin].firstIndex + 1u + geometryVisibilityBins[bin].indexCount * 2u;
    return local + ((geometryVisibilityCandidates[bin].flags & 256u) != 0u ? 0u :
        MeshletGroupMaskWords(bin) * 2u + MeshletFeedbackWords(bin));
}
uint MeshletScanOffset(uint bin)
{
    return MeshletInstanceOffset(bin) + (MeshletHasInstanceBounds(bin) ? 8u : 0u);
}
uint MeshletTileCount(uint bin) { return (geometryVisibilityBins[bin].indexCount + 63u) / 64u; }
uint MeshletAcceptedGroup(uint bin, uint group)
{
    bin = MeshletGroupOwner(bin);
    return (MESHLET_IDS(bin)[MeshletGroupMasks(bin) + group / 16u] >> ((group & 15u) * 2u)) & 3u;
}
uint MeshletSelectedGroup(uint bin, uint group)
{
    bin = MeshletGroupOwner(bin);
    const uint address = MeshletGroupMasks(bin) + MeshletGroupMaskWords(bin) + group / 16u;
    uint flags;
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    if ((geometryVisibilityCandidates[bin].flags & 128u) != 0u)
        flags = meshletViewFirst[NonUniformResourceIndex(geometryVisibilityCandidates[bin].bin)][address];
    else
#elif defined(GEOMETRY_VISIBILITY_RECOVERY)
    if ((geometryVisibilityCandidates[bin].flags & 128u) != 0u)
        flags = meshletFirstStage[address];
    else
#endif
        flags = MESHLET_IDS(bin)[address];
    return (flags >> ((group & 15u) * 2u)) & 3u;
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
    const uint bin = geometryVisibilityBinCount - 1u;
    if (MeshletHasGroups(bin))
        return MeshletScanOffset(bin) +
            ((geometryVisibilityCandidates[bin].flags & 16u) != 0u ? MeshletTileCount(bin) : 0u);
    const GeometryVisibilityBin last = geometryVisibilityBins[geometryVisibilityBinCount - 1u];
    const bool feedback = (geometryVisibilityCandidates[geometryVisibilityBinCount - 1u].flags & 32u) != 0u;
    return last.firstIndex + last.indexCount * (feedback ? 3u : 2u) + 1u;
#endif
}
// Header: published meshlets, completed meshlets, faults, working waves, head, tail,
// initial partition cursor, full retries, empty reads, capacity, nonempty partitions,
// active wave width; JOB_COUNT, producer leases, consumer leases, rejected partitions.
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

void GeometryGroupReset(uint bin, uint lane);

[shader("compute")]
[numthreads(64, 1, 1)]
void ShadowMeshletResetCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (geometryVisibilityBinCount == 0u) return;
    if (MeshletHasGroups(0u))
    {
        const uint bin = group.y * 65535u + group.x;
        if (bin < geometryVisibilityBinCount) GeometryGroupReset(bin, lane);
        return;
    }
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
    const float4 c0 = meshletWorldColumns[MeshletWorldBase(bin)], c1 = meshletWorldColumns[MeshletWorldBase(bin) + 1u],
                 c2 = meshletWorldColumns[MeshletWorldBase(bin) + 2u];
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

uint GeometryGroupAcceptance(uint bin, uint group)
{
    const uint base = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(76u);
    if (base == 0u) return group == 0u ? 3u : 0u;
    if (group >= shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base)) return 0u;
    // Projected error is evaluated once for this view/instance/group. Resident
    // acceptance differs only in complete-child fallback, not in its metric.
    const bool desired = GeometryAcceptClusterGroup(bin, base, group, true);
    bool resident = false;
    if (MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)], group))
    {
        const uint node = base + 16u + group * 64u;
        const uint child = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node + 8u);
        const uint second = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(node + 12u);
        resident = desired || (child != 0xffffffffu &&
            (!MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)], child) ||
             !MeteorGroupResident(shadowMeshletTopology[NonUniformResourceIndex(bin)], second)));
    }
    return uint(desired) | (uint(resident) << 1u);
}

uint GeometrySelectedGroupFlags(uint bin, uint group);

groupshared float3 geometryInstanceMinimum[64];
groupshared float3 geometryInstanceMaximum[64];
groupshared uint geometryInstanceValid[64];

void GeometryResetInstanceBounds(uint bin, uint lane)
{
    if (!MeshletHasInstanceBounds(bin))
    {
        return;
    }
    const uint address = MeshletInstanceOffset(bin);
    const uint worldBase = MeshletWorldBase(bin);
    const float4 minimum = meshletWorldColumns[worldBase + 3u];
    const float4 maximum = meshletWorldColumns[worldBase + 4u];
    if (minimum.w == 0.0f || maximum.w == 0.0f || !GeometryValidInstanceBounds(minimum.xyz, maximum.xyz))
    {
        if (lane == 0u)
        {
            MESHLET_IDS(bin)[address + 6u] = 0u;
            MESHLET_IDS(bin)[address + 7u] = 1u;
        }
        return;
    }
    // 가중치가 모두 0인 정점은 bind pose를 쓴다. 본의 변환 경계와 함께
    // 보존하여 현재 포즈가 원래 메시 경계를 벗어나도 조기 컬링에서 빠지지 않는다.
    float3 posedMinimum = minimum.xyz;
    float3 posedMaximum = maximum.xyz;
    bool valid = true;
    const uint boneCount = MeteorPoseWord(shadowMeshletVertices[NonUniformResourceIndex(bin)], 0u);
    for (uint bone = lane; bone < boneCount; bone += 64u)
    {
        const float4x4 matrix = MeteorBone(shadowMeshletVertices[NonUniformResourceIndex(bin)], bone);
        float4 columns[3];
        [unroll] for (uint column = 0u; column < 3u; ++column)
        {
            columns[column] = float4(matrix[0][column], matrix[1][column], matrix[2][column], matrix[3][column]);
        }
        float3 lower, upper;
        valid = GeometryTransformInstanceBounds(minimum.xyz, maximum.xyz, columns, lower, upper) && valid;
        posedMinimum = min(posedMinimum, lower);
        posedMaximum = max(posedMaximum, upper);
    }
    if (boneCount != 0u)
    {
        geometryInstanceMinimum[lane] = posedMinimum;
        geometryInstanceMaximum[lane] = posedMaximum;
        geometryInstanceValid[lane] = uint(valid);
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint stride = 32u; stride != 0u; stride >>= 1u)
        {
            if (lane < stride)
            {
                geometryInstanceMinimum[lane] =
                    min(geometryInstanceMinimum[lane], geometryInstanceMinimum[lane + stride]);
                geometryInstanceMaximum[lane] =
                    max(geometryInstanceMaximum[lane], geometryInstanceMaximum[lane + stride]);
                geometryInstanceValid[lane] &= geometryInstanceValid[lane + stride];
            }
            GroupMemoryBarrierWithGroupSync();
        }
        posedMinimum = geometryInstanceMinimum[0];
        posedMaximum = geometryInstanceMaximum[0];
        valid = geometryInstanceValid[0] != 0u;
    }
    if (lane == 0u)
    {
        float4 columns[3];
        [unroll] for (uint column = 0u; column < 3u; ++column)
        {
            columns[column] = meshletWorldColumns[worldBase + column];
        }
        float3 lower, upper;
        valid = GeometryTransformInstanceBounds(posedMinimum, posedMaximum, columns, lower, upper) && valid;
        [unroll] for (uint axis = 0u; axis < 3u; ++axis)
        {
            MESHLET_IDS(bin)[address + axis] = asuint(lower[axis]);
            MESHLET_IDS(bin)[address + 3u + axis] = asuint(upper[axis]);
        }
        const MeshletViewData view = MeshletView(bin);
        MESHLET_IDS(bin)[address + 6u] = uint(valid);
        MESHLET_IDS(bin)
        [address + 7u] = uint(!valid || !GeometryInstanceOutsideFrustum(lower, upper, view.projection, view.valid));
    }
}

void GeometryGroupReset(uint bin, uint lane)
{
    GeometryResetInstanceBounds(bin, lane);
    if (lane == 0u)
    {
        GeometryIndirectArguments args;
        args.indexCount = 0u; args.instanceCount = 1u; args.firstIndex = 1u;
        args.baseVertex = 0; args.firstInstance = 0u;
        MESHLET_ARGS(bin) = args;
        MESHLET_IDS(bin)[geometryVisibilityBins[bin].firstIndex] = 0u;
    }
    if (bin == 0u && MeshletUsesWorkQueue())
    {
        const uint queue = MeshletWorkQueueBase();
        if (lane < 16u) geometryVisibleIds[queue + lane] = lane == 9u ? MeshletRingCapacity() : 0u;
        for (uint slot = lane; slot < MeshletRingCapacity(); slot += 64u)
            geometryVisibleIds[queue + 16u + slot] = 0u;
    }
    // Clear before the cull dispatch, not in a producer immediately before a
    // ring CAS: the ring publishes only its atomic job word, not other payload.
    for (uint meshlet = lane; meshlet < geometryVisibilityBins[bin].indexCount; meshlet += 64u)
        MESHLET_IDS(bin)[geometryVisibilityBins[bin].firstIndex + 1u +
            geometryVisibilityBins[bin].indexCount + meshlet] = 0u;
    // First/recovery share immutable LOD masks and feedback. Their classification
    // and visible IDs stay separate because their occlusion tests differ.
    if ((geometryVisibilityCandidates[bin].flags & (128u | 256u)) != 0u) return;
    const uint maskWords = MeshletGroupMaskWords(bin);
    for (uint word = lane; word < maskWords * 2u + MeshletFeedbackWords(bin); word += 64u)
        MESHLET_IDS(bin)[MeshletGroupMasks(bin) + word] = 0u;
    DeviceMemoryBarrierWithGroupSync();
    for (uint group = lane; group < MeshletGroupCount(bin); group += 64u)
    {
        const uint accepted = GeometryGroupAcceptance(bin, group);
        if (accepted != 0u)
            InterlockedOr(MESHLET_IDS(bin)[MeshletGroupMasks(bin) + group / 16u], accepted << ((group & 15u) * 2u));
    }
    // One threadgroup owns the complete group's masks. A local device-memory
    // barrier replaces another dispatch; other bins consume only after Reset.
    DeviceMemoryBarrierWithGroupSync();
    for (uint group = lane; group < MeshletGroupCount(bin); group += 64u)
    {
        const uint selected = GeometrySelectedGroupFlags(bin, group);
        if (selected != 0u)
            InterlockedOr(MESHLET_IDS(bin)[MeshletGroupMasks(bin) + MeshletGroupMaskWords(bin) + group / 16u],
                selected << ((group & 15u) * 2u));
        if (MeshletHasInstanceBounds(bin) && (geometryVisibilityCandidates[bin].flags & 32u) != 0u &&
            (selected & 1u) != 0u)
        {
            // 큐에 들어가지 않은 인스턴스도 기존 desired-cut 요청을 유지한다.
            // 같은 그룹의 메시렛 수만큼 반복되던 원자적 갱신을 한 번으로 줄인다.
            InterlockedOr(MESHLET_IDS(bin)[MeshletFeedbackOffset(bin) + group / 32u], 1u << (group & 31u));
        }
    }
}

uint GeometrySelectedGroupFlags(uint bin, uint group)
{
    uint flags = MeshletAcceptedGroup(bin, group);
    if (flags == 0u) return 0u;
    const uint base = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(76u);
    if (base == 0u) return flags;
    uint parent = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base + 16u + group * 64u + 16u);
    [loop] for (uint depth = 0u; depth < 8u; ++depth)
    {
        if (parent == 0xffffffffu) return flags;
        if (parent >= MeshletGroupCount(bin)) return 0u;
        flags &= ~MeshletAcceptedGroup(bin, parent);
        if (flags == 0u) return 0u;
        parent = shadowMeshletTopology[NonUniformResourceIndex(bin)].Load(base + 16u + parent * 64u + 16u);
    }
    return 0u; // Preserve the cooked hierarchy's bounded-depth contract.
}

bool GeometryPrepareMeshlet(uint bin, uint meshlet)
{
    if (!MeshletHasGroups(bin)) return true; // Fixed-range legacy oracle.
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(shadowMeshletTopology[NonUniformResourceIndex(bin)], meshlet);
    if (descriptor.groupId >= MeshletGroupCount(bin)) return false;
    const uint owner = MeshletGroupOwner(bin);
    const uint selected = MeshletSelectedGroup(owner, descriptor.groupId);
    if (!MeshletHasInstanceBounds(bin) && (geometryVisibilityCandidates[bin].flags & (32u | 128u)) == 32u &&
        (selected & 1u) != 0u)
    {
        // One bit per group, including resident keepalive. No view/stage-sized
        // list of per-meshlet uint demands crosses back to the CPU.
        InterlockedOr(MESHLET_IDS(owner)[MeshletFeedbackOffset(owner) + descriptor.groupId / 32u],
            1u << (descriptor.groupId & 31u));
    }
    return (selected & 2u) != 0u &&
        MeteorClusterResident(shadowMeshletTopology[NonUniformResourceIndex(bin)], meshlet);
}

bool GeometryMeshletBoxOccluded(uint bin, float3 minimumWorld, float3 maximumWorld, uint firstAddress)
{
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    const MeshletViewData view = MeshletView(bin);
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    const uint viewId = geometryVisibilityCandidates[bin].bin;
    bool testOcclusion = view.mode != 0u;
    if (view.mode == 3u)
    {
        // 인스턴스 판정은 첫 단계의 총 가시 개수, 메시렛 판정은 해당
        // classification을 읽는다. 첫 단계 생존자는 recovery에서 제거하지 않는다.
        testOcclusion = meshletViewFirst[NonUniformResourceIndex(viewId)][firstAddress] == 0u;
    }
    float4 projection[4];
    [unroll] for (uint column = 0u; column < 4u; ++column)
    {
        projection[column] = view.mode == 2u || view.mode == 4u ? view.history[column] : view.projection[column];
    }
    if (view.mode == 4u && !GeometryPredictionContainsBounds(minimumWorld, maximumWorld, projection))
    {
        return false;
    }
    return testOcclusion && GeometrySharedBoxOccluded(minimumWorld, maximumWorld, projection,
                                                      uint2(view.width, view.height), view.levels, view.valid, viewId);
#else
    bool testOcclusion = true;
#if defined(GEOMETRY_VISIBILITY_RECOVERY)
    testOcclusion = meshletFirstStage[firstAddress] == 0u;
#endif
#if defined(GEOMETRY_VISIBILITY_CAMERA_PREDICTION)
    if (!GeometryPredictionContainsBounds(minimumWorld, maximumWorld, meshletHistoryColumns))
    {
        return false;
    }
#endif
    return testOcclusion && GeometryBoxOccluded(minimumWorld, maximumWorld,
#if defined(GEOMETRY_VISIBILITY_HISTORY) || defined(GEOMETRY_VISIBILITY_CAMERA_PREDICTION)
                                                meshletHistoryColumns,
#else
                                                view.projection,
#endif
                                                uint2(view.width, view.height), view.levels, view.valid);
#endif
#else
    return false;
#endif
}

bool GeometryInstanceAdmitted(uint bin)
{
    if (!MeshletHasInstanceBounds(bin))
    {
        return true;
    }
    const uint address = MeshletInstanceOffset(bin);
    if (MESHLET_IDS(bin)[address + 7u] == 0u)
    {
        return false;
    }
    if (MESHLET_IDS(bin)[address + 6u] == 0u)
    {
        return true;
    }
    const float3 minimum =
        asfloat(uint3(MESHLET_IDS(bin)[address], MESHLET_IDS(bin)[address + 1u], MESHLET_IDS(bin)[address + 2u]));
    const float3 maximum =
        asfloat(uint3(MESHLET_IDS(bin)[address + 3u], MESHLET_IDS(bin)[address + 4u], MESHLET_IDS(bin)[address + 5u]));
    return !GeometryMeshletBoxOccluded(bin, minimum, maximum, geometryVisibilityBins[bin].firstIndex);
}

void GeometryCullMeshlet(uint bin, uint meshlet)
{
    const MeshletViewData view = MeshletView(bin);
    if (meshlet >= geometryVisibilityBins[bin].indexCount) return;
    const uint classification = geometryVisibilityBins[bin].firstIndex + 1u +
        geometryVisibilityBins[bin].indexCount + meshlet;
    const bool classify = (geometryVisibilityCandidates[bin].flags & 8u) != 0u;
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(shadowMeshletTopology[NonUniformResourceIndex(bin)], meshlet);
    if (!MeshletHasGroups(bin))
    {
        if (classify) MESHLET_IDS(bin)[classification] = 0u;
        if ((geometryVisibilityCandidates[bin].flags & 32u) != 0u)
        {
            const uint feedback = geometryVisibilityBins[bin].firstIndex + 1u +
                geometryVisibilityBins[bin].indexCount * 2u + meshlet;
            MESHLET_IDS(bin)[feedback] = GeometryClusterSelected(bin, descriptor.groupId, true) ? 1u : 0u;
        }
        if (!MeteorClusterResident(shadowMeshletTopology[NonUniformResourceIndex(bin)], meshlet) ||
            !GeometryClusterSelected(bin, descriptor.groupId, false)) return;
    }
    const uint count = descriptor.vertexCount;
    uint outside = view.valid != 0u ? 63u : 0u;
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    float3 minimumWorld = float3(3.402823466e+38f);
    float3 maximumWorld = -minimumWorld;
    bool finiteBounds = count != 0u;
#endif
    for (uint vertex = 0u; vertex < count; ++vertex)
    {
        const float4 object =
            float4(MeteorGeometryPosition(
                       shadowMeshletVertices[NonUniformResourceIndex(bin)],
                       shadowMeshletTopology[NonUniformResourceIndex(bin)],
                       LXMeshletVertex(shadowMeshletTopology[NonUniformResourceIndex(bin)], descriptor, vertex)),
                   1);
        const float4 position = float4(dot(object, meshletWorldColumns[MeshletWorldBase(bin)]),
                                       dot(object, meshletWorldColumns[MeshletWorldBase(bin) + 1u]),
                                       dot(object, meshletWorldColumns[MeshletWorldBase(bin) + 2u]), 1);
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
        if (finiteBounds && GeometryMeshletBoxOccluded(bin, minimumWorld, maximumWorld, classification))
        {
            return;
        }
#endif
        if (classify) MESHLET_IDS(bin)[classification] = 1u;
        // The later ordered compaction chain preserves source meshlet order.
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
        const uint meshlet = (workGroup - geometryVisibilityCandidates[low].sourceIndex) * 64u + lane;
        if (meshlet < geometryVisibilityBins[low].indexCount && GeometryPrepareMeshlet(low, meshlet))
            GeometryCullMeshlet(low, meshlet);
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
                    if (sourceRemaining != 0u)
                    {
                        uint admitted = 1u;
                        if (WaveIsFirstLane())
                        {
                            InterlockedAdd(geometryVisibleIds[queue + 10u], 1u);
                            admitted = uint(GeometryInstanceAdmitted(sourceBin));
                            if (admitted == 0u)
                            {
                                InterlockedAdd(geometryVisibleIds[queue + 15u], 1u);
                            }
                        }
                        if (WaveReadLaneFirst(admitted) == 0u)
                        {
                            sourceRemaining = 0u;
                        }
                    }
                }
            }
            if (!WaveActiveAnyTrue(pending != 0u) && sourceRemaining != 0u)
            {
                const uint batch = min(sourceRemaining, workerWidth);
                if (workerLane < batch)
                {
                    const uint meshlet = sourceOffset + workerLane;
                    // The GPU group cut removes unselected/nonresident work
                    // before it contends for the bounded ring. Demand still
                    // records the desired cut, including resident keepalive.
                    if (GeometryPrepareMeshlet(sourceBin, meshlet))
                        pending = MeshletJobBase(sourceBin) + 1u + meshlet;
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

groupshared uint geometryCompactScan[64];
groupshared uint geometryCompactCarry;

uint GeometryCompactPrefix(uint lane, uint value)
{
    geometryCompactScan[lane] = value;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 1u; stride < 64u; stride <<= 1u)
    {
        const uint previous = lane >= stride ? geometryCompactScan[lane - stride] : 0u;
        GroupMemoryBarrierWithGroupSync();
        geometryCompactScan[lane] += previous;
        GroupMemoryBarrierWithGroupSync();
    }
    return geometryCompactScan[lane];
}

bool GeometryCompactTile(uint3 group, out uint bin, out uint tile)
{
    bin = 0u; tile = 0u;
    if (geometryVisibilityBinCount == 0u) return false;
    const uint work = group.y * 65535u + group.x;
    const uint last = geometryVisibilityBinCount - 1u;
    if (work >= geometryVisibilityBins[last].preservedInstanceCount + MeshletTileCount(last)) return false;
    uint high = geometryVisibilityBinCount;
    [loop] while (bin + 1u < high)
    {
        const uint middle = bin + (high - bin) / 2u;
        if (geometryVisibilityBins[middle].preservedInstanceCount <= work) bin = middle;
        else high = middle;
    }
    tile = work - geometryVisibilityBins[bin].preservedInstanceCount;
    return MeshletHasGroups(bin) && (geometryVisibilityCandidates[bin].flags & 16u) != 0u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void MeshletCountCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint bin, tile;
    if (!GeometryCompactTile(group, bin, tile)) return;
    const uint meshlet = tile * 64u + lane;
    const uint total = geometryVisibilityBins[bin].indexCount;
    const uint first = geometryVisibilityBins[bin].firstIndex;
    const uint visible = meshlet < total ? uint(MESHLET_IDS(bin)[first + 1u + total + meshlet] != 0u) : 0u;
    const uint prefix = GeometryCompactPrefix(lane, visible);
    if (lane == 63u) MESHLET_IDS(bin)[MeshletScanOffset(bin) + tile] = prefix;
}

void GeometryWriteMeshletArguments(uint bin, uint count)
{
    MESHLET_ARGS(bin).indexCount = min(count, 65535u);
    MESHLET_ARGS(bin).instanceCount = max(1u, (count + 65534u) / 65535u);
    MESHLET_ARGS(bin).firstIndex = 1u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void MeshletFinalizeCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (geometryVisibilityBinCount == 0u) return;
    if (MeshletHasGroups(0u))
    {
        // Unordered views keep the compact 64-bins-per-group finalizer.
        if ((geometryVisibilityPlaneMask & 0x40000000u) == 0u)
        {
            uint bin;
            if (GeometryVisibilityIndex(group, lane, geometryVisibilityBinCount, bin))
                GeometryWriteMeshletArguments(bin, MESHLET_IDS(bin)[geometryVisibilityBins[bin].firstIndex]);
            return;
        }
        const uint bin = group.y * 65535u + group.x;
        if (bin >= geometryVisibilityBinCount) return;
        const uint first = geometryVisibilityBins[bin].firstIndex;
        if ((geometryVisibilityCandidates[bin].flags & 16u) != 0u)
        {
            // Scan only tile totals here. Individual meshlets scatter in a
            // subsequent parallel dispatch; the classification remains intact
            // for first/recovery and source-order consumers.
            const uint tiles = MeshletTileCount(bin);
            if (lane == 0u) geometryCompactCarry = 0u;
            GroupMemoryBarrierWithGroupSync();
            for (uint begin = 0u; begin < tiles; begin += 64u)
            {
                const uint tile = begin + lane;
                const uint value = tile < tiles ? MESHLET_IDS(bin)[MeshletScanOffset(bin) + tile] : 0u;
                const uint inclusive = GeometryCompactPrefix(lane, value);
                const uint carry = geometryCompactCarry;
                GroupMemoryBarrierWithGroupSync();
                if (tile < tiles) MESHLET_IDS(bin)[MeshletScanOffset(bin) + tile] = carry + inclusive - value;
                if (lane == 0u) geometryCompactCarry = carry + geometryCompactScan[63];
                GroupMemoryBarrierWithGroupSync();
            }
            if (lane == 0u)
            {
                MESHLET_IDS(bin)[first] = geometryCompactCarry;
                GeometryWriteMeshletArguments(bin, geometryCompactCarry);
            }
        }
        else if (lane == 0u)
            GeometryWriteMeshletArguments(bin, MESHLET_IDS(bin)[first]);
        return;
    }
    // Preserve the direct shader oracle's original compact ABI. Production
    // uses the group layout and the Count -> prefix -> Scatter chain above.
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
    GeometryWriteMeshletArguments(bin, count);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void MeshletScatterCS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint bin, tile;
    if (!GeometryCompactTile(group, bin, tile)) return;
    const uint meshlet = tile * 64u + lane;
    const uint total = geometryVisibilityBins[bin].indexCount;
    const uint first = geometryVisibilityBins[bin].firstIndex;
    const uint visible = meshlet < total ? uint(MESHLET_IDS(bin)[first + 1u + total + meshlet] != 0u) : 0u;
    const uint inclusive = GeometryCompactPrefix(lane, visible);
    if (visible != 0u)
    {
        const uint offset = MESHLET_IDS(bin)[MeshletScanOffset(bin) + tile];
        MESHLET_IDS(bin)[first + 1u + offset + inclusive - 1u] = meshlet;
    }
}
