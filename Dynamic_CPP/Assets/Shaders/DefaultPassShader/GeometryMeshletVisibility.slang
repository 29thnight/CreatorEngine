// Fixed ABI matches GpuMeshletVisibility and the validated RHIMeshletBufferHeader.
struct MeshletVisibilityWorld
{
    float4 columns[4];
};
struct MeshletDispatchArguments
{
    uint x;
    uint y;
    uint z;
};
cbuffer MeshletVisibilityConstants : register(b0)
{
    float4 meshletVisibilityPlanes[6];
    uint meshletVisibilityMeshletCount;
    uint meshletVisibilityInstanceCount;
    uint meshletVisibilityCandidateCount;
    uint meshletVisibilityPlaneMask;
    float4 meshletVisibilityProjectionColumns[4];
    float4 meshletVisibilityLocalSphere;
    float4 meshletVisibilityLodErrors[2];
    uint2 meshletVisibilityViewSize;
    uint meshletVisibilityLodCount;
    uint meshletVisibilityLodIndex;
    float meshletVisibilityMaxPixelError;
    uint meshletVisibilityProjectionValid;
    uint meshletVisibilityOcclusionLevels;
    uint meshletVisibilityPadding;
};
StructuredBuffer<MeshletVisibilityWorld> meshletVisibilityWorlds : register(t0);
ByteAddressBuffer meshletVisibilityGeometry : register(t1);
RWStructuredBuffer<uint2> meshletVisibilityPairs : register(u0);
RWStructuredBuffer<MeshletDispatchArguments> meshletVisibilityArguments : register(u1);

#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
#include "GeometryOcclusion.slang"
#endif

#if defined(GEOMETRY_MESHLET_RECHECK)
StructuredBuffer<uint2> meshletVisibilitySourcePairs : register(t18);
ByteAddressBuffer meshletVisibilitySourceArguments : register(t19);
#endif

uint MeshletVisibilitySelectLod(MeshletVisibilityWorld world)
{
    if (meshletVisibilityLodCount <= 1u || meshletVisibilityProjectionValid == 0u ||
        any(meshletVisibilityViewSize == 0u) || !all(isfinite(meshletVisibilityLocalSphere)) ||
        meshletVisibilityLocalSphere.w <= 0.0f)
    {
        return 0u;
    }
    const float4 localCenter = float4(meshletVisibilityLocalSphere.xyz, 1.0f);
    const float3 center = float3(dot(localCenter, world.columns[0]),
        dot(localCenter, world.columns[1]), dot(localCenter, world.columns[2]));
    const float scaleSquared = dot(world.columns[0].xyz, world.columns[0].xyz) +
        dot(world.columns[1].xyz, world.columns[1].xyz) + dot(world.columns[2].xyz, world.columns[2].xyz);
    const float worldScale = sqrt(scaleSquared) * 1.000002f;
    const float3 centerMagnitude = float3(dot(abs(localCenter), abs(world.columns[0])),
        dot(abs(localCenter), abs(world.columns[1])), dot(abs(localCenter), abs(world.columns[2])));
    const float radius = meshletVisibilityLocalSphere.w * worldScale + length(centerMagnitude * 0.000002f);
    if (!all(isfinite(center)) || !isfinite(worldScale) || worldScale <= 0.0f || !isfinite(radius))
    {
        return 0u;
    }

    // Bound clip coordinates over the entire transformed base mesh sphere.
    // All authored LODs reuse its vertices. Positive lower W is required even
    // for a camera whose center lies outside the nominal near plane.
    float4 clipCenter, clipRadius, clipError;
    const float4 position = float4(center, 1.0f);
    const float4 magnitude = float4(abs(center) + radius, 1.0f);
    [unroll]
    for (uint component = 0u; component < 4u; ++component)
    {
        const float4 column = meshletVisibilityProjectionColumns[component];
        clipCenter[component] = dot(position, column);
        clipRadius[component] = radius * length(column.xyz) * 1.000002f;
        clipError[component] = max(0.000001f, dot(magnitude, abs(column)) * 0.000002f);
    }
    const float lowW = clipCenter.w - clipRadius.w - clipError.w;
    const float lowZ = clipCenter.z - clipRadius.z - clipError.z;
    if (!all(isfinite(clipCenter)) || !all(isfinite(clipRadius)) || !all(isfinite(clipError)) ||
        !isfinite(lowW) || !isfinite(lowZ) || lowW <= 0.000001f || lowZ <= 0.0f)
    {
        return 0u;
    }

    // f(P)=clip.xy/clip.w. Over the sphere, ||grad f|| is bounded by
    // (||A|| max|W| + ||W.xyz|| max|A(P)|) / min(W)^2. This avoids
    // assuming a symmetric perspective matrix, and also covers orthographic
    // projection, nonuniform object scale and shear. The stored simplifier
    // metric remains a quality heuristic; projection/world scaling is bounded.
    const float maximumW = abs(clipCenter.w) + clipRadius.w + clipError.w;
    const float2 maximumXY = abs(clipCenter.xy) + clipRadius.xy + clipError.xy;
    const float2 projectionScale = float2(length(meshletVisibilityProjectionColumns[0].xyz),
        length(meshletVisibilityProjectionColumns[1].xyz)) * 1.000002f;
    const float perspectiveScale = length(meshletVisibilityProjectionColumns[3].xyz) * 1.000002f;
    const float2 derivative = (projectionScale * maximumW + perspectiveScale * maximumXY) / (lowW * lowW);
    const float pixelScale = length(derivative * float2(meshletVisibilityViewSize) * 0.5f) * worldScale * 1.000004f;
    if (!all(isfinite(derivative)) || !isfinite(pixelScale) || pixelScale <= 0.0f)
    {
        return 0u;
    }

    uint selected = 0u;
    for (uint level = 1u; level < meshletVisibilityLodCount; ++level)
    {
        const float error = meshletVisibilityLodErrors[level / 4u][level % 4u];
        const float projectedError = error * pixelScale;
        if (!isfinite(error) || error < 0.0f || !isfinite(projectedError))
        {
            return 0u;
        }
        if (projectedError <= meshletVisibilityMaxPixelError)
        {
            selected = level;
        }
    }
    return selected;
}

bool MeshletVisibilitySphereVisible(float4 sphere)
{
    if (!all(isfinite(sphere)) || sphere.w <= 0.0)
    {
        return true;
    }
    for (uint planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        if ((meshletVisibilityPlaneMask & (1u << planeIndex)) == 0u)
        {
            continue;
        }
        const float4 plane = meshletVisibilityPlanes[planeIndex];
        const float distance = dot(plane.xyz, sphere.xyz) + plane.w;
        const float magnitude = dot(abs(plane.xyz), abs(sphere.xyz)) + abs(plane.w) + sphere.w;
        const float tolerance = max(0.0001, magnitude * 0.000001);
        if (!isfinite(distance) || !isfinite(tolerance))
        {
            return true;
        }
        if (distance < -sphere.w - tolerance)
        {
            return false;
        }
    }
    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GeometryMeshletResetCS(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x == 0u)
    {
        MeshletDispatchArguments arguments;
        arguments.x = 0u;
        arguments.y = 1u;
        arguments.z = 1u;
        meshletVisibilityArguments[0] = arguments;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void GeometryMeshletCullCS(uint3 thread : SV_DispatchThreadID)
{
    const uint candidate = thread.x;
    if (candidate >= meshletVisibilityCandidateCount)
    {
        return;
    }
#if defined(GEOMETRY_MESHLET_RECHECK)
    const uint sourceCount = min(meshletVisibilitySourceArguments.Load(0u), meshletVisibilityCandidateCount);
    if (candidate >= sourceCount)
    {
        return;
    }
    const uint2 pair = meshletVisibilitySourcePairs[candidate];
    const uint instance = pair.x;
    const uint meshlet = pair.y;
    if (instance >= meshletVisibilityInstanceCount || meshlet >= meshletVisibilityMeshletCount)
    {
        return;
    }
#else
    const uint instance = candidate / meshletVisibilityMeshletCount;
    const uint meshlet = candidate % meshletVisibilityMeshletCount;
#endif
    const MeshletVisibilityWorld world = meshletVisibilityWorlds[instance];
#if !defined(GEOMETRY_MESHLET_RECHECK)
    if (MeshletVisibilitySelectLod(world) != meshletVisibilityLodIndex)
    {
        return;
    }
#endif
    // Header descriptorOffset at byte12, descriptorStride at byte16. Profile1
    // descriptor sphere is float4 at byte32 of each 80-byte descriptor.
    const uint descriptorOffset = meshletVisibilityGeometry.Load(12u);
    const uint descriptorStride = meshletVisibilityGeometry.Load(16u);
    const float4 localSphere = asfloat(meshletVisibilityGeometry.Load4(
        descriptorOffset + meshlet * descriptorStride + 32u));
    const float4 localCenter = float4(localSphere.xyz, 1.0);
    const float3 center = float3(dot(localCenter, world.columns[0]),
        dot(localCenter, world.columns[1]), dot(localCenter, world.columns[2]));
    // Frobenius bounds nonuniform scale and shear. Invalid/overflowing results
    // retain the pair; no cone rejection is inferred for mirrored/two-sided data.
    const float scaleSquared = dot(world.columns[0].xyz, world.columns[0].xyz) +
        dot(world.columns[1].xyz, world.columns[1].xyz) + dot(world.columns[2].xyz, world.columns[2].xyz);
    // Large local positions and translation can cancel into a small center.
    // Bound the arithmetic using the un-cancelled products, not abs(center).
    const float3 centerMagnitude = float3(dot(abs(localCenter), abs(world.columns[0])),
        dot(abs(localCenter), abs(world.columns[1])), dot(abs(localCenter), abs(world.columns[2])));
    const float transformRoundoff = length(centerMagnitude * 0.000002);
    const float radius = localSphere.w * sqrt(scaleSquared);
    const float4 sphere = float4(center, radius * 1.000002 + transformRoundoff);
    // Zero/invalid local bounds remain unknown even if the inflation is positive.
    if (all(isfinite(localSphere)) && localSphere.w > 0.0 && !MeshletVisibilitySphereVisible(sphere))
    {
        return;
    }
#if defined(GEOMETRY_VISIBILITY_OCCLUSION)
    if (GeometrySphereOccluded(sphere, !all(isfinite(localSphere)) || localSphere.w <= 0.0f,
        meshletVisibilityProjectionColumns, meshletVisibilityViewSize,
        meshletVisibilityOcclusionLevels, meshletVisibilityProjectionValid))
    {
        return;
    }
#endif
    uint slot;
    // There is at most one reservation per candidate and the CPU reserves the
    // entire Cartesian product. X cannot exceed either capacity or device limits.
    InterlockedAdd(meshletVisibilityArguments[0].x, 1u, slot);
    meshletVisibilityPairs[slot] = uint2(instance, meshlet);
}
