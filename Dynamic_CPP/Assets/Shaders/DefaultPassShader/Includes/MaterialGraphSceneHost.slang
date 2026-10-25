#pragma once
#define LX_SCENE_VOLUME_TRANSPORT 1
#include "MaterialGraphSurfacePoint.slang"
#include "MaterialEvaluation.slang"
#include "PrincipledIblLookup.slang"
#include "PbrCoverage.slang"
#include "CascadedShadow.slang"
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0
#include "SpecialForwardSurface.slang"
#include "MaterialGraphSceneSubsurface.slang"
#else
#include "PbrSurface.slang"
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
#include "MaterialGraphSceneRefraction.slang"
#endif

struct LXSceneLight
{
    float4 position;
    float4 direction;
    float4 color;
    float4 attenuation;
};

cbuffer LXSceneConstants : register(b0)
{
    float4x4 lx_viewProjection;
    float4 lx_eye;
    uint lx_drawOwner;
    uint lx_coverage;
    float lx_cutoff;
    uint lx_hasEnvironment;
    uint lx_lightCount;
    uint lx_hasShadow;
    float lx_shadowBlend;
    uint lx_width;
    uint lx_height;
    uint lx_forwardTiles;
    uint lx_tileGridX;
    uint lx_blended;
    float4x4 lx_shadowProjection[3];
    float4 lx_shadowSplits;
    float4 lx_shadowBias;
    float4 lx_cameraForward;
    // Flat host ABI: each light has position, direction, color, attenuation.
    float4 lx_lights[256];
    float4x4 lx_temporalCurrentProjection;
    float4x4 lx_temporalPreviousProjection;
    uint lx_temporalHistoryValid;
    uint3 lx_temporalPadding;
    uint2 lx_geometryKey;
    uint2 lx_geometryObjectId;
    uint2 lx_geometryIncarnation;
    uint2 lx_geometryInstanceId;
    uint2 lx_geometryMaterialSlot;
    uint lx_geometryLodIndex;
    uint lx_geometryPrimitiveCount;
    uint lx_geometryIdentityVersion;
    uint3 lx_geometryIdentityPadding;
    // lx_vertices is object space; these map the current/previous chunk to world.
    float4x4 lx_world;
    float4x4 lx_previousWorld;
};

StructuredBuffer<float4> lx_vertices : register(t0);
ByteAddressBuffer lx_meshlets : register(t155);
#include "MeshletGeometryAccess.slang"
#include "LxMeshletGpuContract.slang"
#include "MaterialGraphSceneRecord.slang"
#include "MaterialGraphPrimitiveCacheTypes.slang"
StructuredBuffer<LXSceneGpuRecord> lx_sceneRecords : register(t170);
StructuredBuffer<uint2> lx_scenePackets : register(t171);
StructuredBuffer<uint4> lx_scenePacketRanges : register(t173);
StructuredBuffer<uint> lx_primitiveCacheKeys : register(t174);
StructuredBuffer<LXPrimitiveCacheTriangle> lx_primitiveCacheData : register(t175);
cbuffer LXScenePacketSelector : register(b8) { uint4 lx_packetSelector; };
StructuredBuffer<float4> lx_packetVertices[64] : register(t256);
ByteAddressBuffer lx_packetTopology[64] : register(t512);
StructuredBuffer<float4> lx_previousVertices : register(t156);

uint LXSceneOwner(uint record) { return record != 0u ? lx_sceneRecords[record].material.z : lx_drawOwner; }
uint LXSceneCoverage(uint record) { return record != 0u ? lx_sceneRecords[record].material.w : lx_coverage; }
float LXSceneCutoff(uint record) { return record != 0u ? asfloat(lx_sceneRecords[record].surface.x) : lx_cutoff; }
uint LXSceneBlended(uint record) { return record != 0u ? 0u : lx_blended; }
uint LXSceneHistoryValid(uint record) { return record != 0u ? lx_sceneRecords[record].history.x : lx_temporalHistoryValid; }
uint3 LXSceneIdentity(uint record)
{
    return record != 0u ? uint3(lx_sceneRecords[record].surface.y, lx_sceneRecords[record].identity.reserved.yz)
                        : lx_geometryIdentityPadding;
}
float4x4 LXSceneWorld(uint record, bool previous = false)
{
    if (record == 0u) return previous ? lx_previousWorld : lx_world;
    return LXPrimitiveWorld(lx_sceneRecords[record], previous);
}
LXGeneratedMaterial LXSceneGenerateMaterial(LXMaterialContext context, uint record)
{
    if (record == 0u) return LXGenerateMaterial(context, LXBoundMaterialParameters());
    const uint2 bytes = lx_sceneRecords[record].material.xy;
    return LXGeneratePacketMaterial(context, LXBoundMaterialParameters(bytes.x, bytes.y));
}
LXSurfacePoint LXSceneGeometryPoint(uint record, uint vertex, bool previous = false)
{
    if (record == 0u)
        return previous ? MeteorGeometryPoint(lx_previousVertices, lx_meshlets, vertex)
                        : MeteorGeometryPoint(lx_vertices, lx_meshlets, vertex);
    const uint4 geometry = lx_sceneRecords[record].geometry;
    const uint pose = previous ? geometry.y : geometry.x;
    return MeteorGeometryPoint(lx_packetVertices[NonUniformResourceIndex(pose)],
                               lx_packetTopology[NonUniformResourceIndex(geometry.z)], vertex);
}
LXMeshletDescriptor LXSceneMeshlet(uint record, uint meshlet)
{
    if (record == 0u) return LXLoadMeshlet(lx_meshlets, meshlet);
    return LXLoadMeshlet(lx_packetTopology[NonUniformResourceIndex(lx_sceneRecords[record].geometry.z)], meshlet);
}
uint LXSceneMeshletVertex(uint record, LXMeshletDescriptor descriptor, uint vertex)
{
    if (record == 0u) return LXMeshletVertex(lx_meshlets, descriptor, vertex);
    return LXMeshletVertex(lx_packetTopology[NonUniformResourceIndex(lx_sceneRecords[record].geometry.z)], descriptor, vertex);
}
LXGeometryPrimitive LXScenePrimitiveAt(uint record, LXMeshletDescriptor descriptor, uint triangle)
{
    if (record == 0u) return LXLoadGeometryPrimitive(lx_meshlets, descriptor, triangle);
    return LXLoadGeometryPrimitive(lx_packetTopology[NonUniformResourceIndex(lx_sceneRecords[record].geometry.z)], descriptor, triangle);
}
// GPU compaction emits the original draw owners; direct paths bind one owner.
StructuredBuffer<uint> lx_visibleOwners : register(t138);
TextureCube<float4> gEnvironment : register(t1);
Texture2D<uint> lx_owners : register(t2);
Texture2D<float2> lx_ao : register(t3);
Texture2DArray<float> lx_shadow : register(t4);
SamplerState gEnvironmentSampler : register(s0);
SamplerComparisonState lx_shadowSampler : register(s1);
#define lx_volumeEnvironmentSampler gEnvironmentSampler
#define lx_volumeShadowSampler lx_shadowSampler
#include "MaterialGraphForwardVolume.slang"
StructuredBuffer<PrincipledIblBakeSample> lx_ibl : register(t5);
TextureCube<float4> lx_runtimeIrradiance : register(t135);
TextureCube<float4> lx_runtimePrefiltered : register(t136);
Texture2D<float4> lx_runtimeSource : register(t137);
cbuffer LXSceneRuntimeLookupConstants : register(b6)
{
    uint lx_runtimeLookup, lx_runtimePrecomputed, lx_runtimeHasSource, lx_runtimeEnvironment;
};

float3 LXSceneRuntimeRadiance(float3 direction)
{
    if (lx_runtimeHasSource != 0u)
    {
        const float2 uv = float2(atan2(direction.z, direction.x) / (2.0 * PI) + 0.5,
            -asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
        uint width, height;
        lx_runtimeSource.GetDimensions(width, height);
        const float2 pixel = uv * float2(width, height) - 0.5;
        const int2 cell = int2(floor(pixel));
        const float2 weight = frac(pixel);
        const int x0 = (cell.x + int(width)) % int(width), x1 = (x0 + 1) % int(width);
        const int y0 = clamp(cell.y, 0, int(height) - 1), y1 = clamp(cell.y + 1, 0, int(height) - 1);
        return SanitizeEnvironmentRadiance(lerp(
            lerp(lx_runtimeSource.Load(int3(x0, y0, 0)).rgb, lx_runtimeSource.Load(int3(x1, y0, 0)).rgb, weight.x),
            lerp(lx_runtimeSource.Load(int3(x0, y1, 0)).rgb, lx_runtimeSource.Load(int3(x1, y1, 0)).rgb, weight.x),
            weight.y));
    }
    return SanitizeEnvironmentRadiance(gEnvironment.SampleLevel(gEnvironmentSampler, direction, 0).rgb);
}

#define LX_SPLIT_SUM_PRECOMPUTED lx_runtimePrecomputed
#define LX_SPLIT_SUM_ENVIRONMENT lx_runtimeEnvironment
#define LX_SPLIT_SUM_IRRADIANCE lx_runtimeIrradiance
#define LX_SPLIT_SUM_PREFILTERED lx_runtimePrefiltered
#define LX_SPLIT_SUM_SAMPLER gEnvironmentSampler
#define LX_SPLIT_SUM_RADIANCE LXSceneRuntimeRadiance
#include "PrincipledRuntimeIbl.slang"
StructuredBuffer<float4> lx_forwardLights : register(t128);
StructuredBuffer<uint> lx_forwardCounts : register(t129);
StructuredBuffer<uint> lx_forwardIndices : register(t130);

uint LXSceneLightCount(float2 pixel, out uint tile, out bool tiled)
{
    tile = 0u;
    tiled = false;
    if (lx_forwardTiles == 0u) return lx_lightCount;
    tile = uint(pixel.y) / 16u * lx_tileGridX + uint(pixel.x) / 16u;
    const uint tiles = lx_tileGridX * ((lx_height + 15u) / 16u);
    tiled = lx_forwardTiles == 1u && lx_forwardCounts[tiles + tile] <= 32u;
    return tiled ? lx_forwardCounts[tile] : lx_lightCount;
}

LXSceneLight LXSceneReadLight(uint index)
{
    LXSceneLight light;
    light.position = lx_forwardTiles != 0u ? lx_forwardLights[index * 4u] : lx_lights[index * 4u];
    light.direction = lx_forwardTiles != 0u ? lx_forwardLights[index * 4u + 1u] : lx_lights[index * 4u + 1u];
    light.color = lx_forwardTiles != 0u ? lx_forwardLights[index * 4u + 2u] : lx_lights[index * 4u + 2u];
    light.attenuation = lx_forwardTiles != 0u ? lx_forwardLights[index * 4u + 3u] : lx_lights[index * 4u + 3u];
    return light;
}
Texture2D<float4> lx_decalColor : register(t9);
Texture2D<float4> lx_decalOrm : register(t10);
Texture2D<float4> lx_decalNormal : register(t11);
Texture2D<float4> lx_beforeDecalColor : register(t12);
Texture2D<float4> lx_beforeDecalOrm : register(t13);
Texture2D<float4> lx_beforeDecalNormal : register(t14);
cbuffer LXSceneDecalConstants : register(b3)
{
    uint lx_decalEnabled;
    uint3 lx_decalPadding;
};
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0
StructuredBuffer<LXSceneSubsurfaceReflection> lx_subsurfaceReflection : register(t6);
StructuredBuffer<float4> lx_subsurfaceIrradiance : register(t7);
#include "MaterialGraphRuntimeEffectContract.slang"
Texture2D<float4> lx_runtimeEffect0 : register(t139);
Texture2D<float4> lx_runtimeEffect1 : register(t140);
Texture2D<float4> lx_runtimeEffect2 : register(t141);
Texture2D<float4> lx_runtimeEffect3 : register(t142);
Texture2D<float4> lx_runtimeEffect4 : register(t143);
Texture2D<float4> lx_runtimeEffect5 : register(t144);
Texture2D<float4> lx_runtimeEffect6 : register(t145);
Texture2D<float4> lx_runtimeEffect7 : register(t146);
Texture2D<float4> lx_runtimeEffect8 : register(t147);
Texture2D<float4> lx_runtimeEffect9 : register(t148);
Texture2D<float4> lx_runtimeEffect10 : register(t149);
Texture2D<float4> lx_runtimeEffect11 : register(t150);
Texture2D<float4> lx_runtimeEffect12 : register(t151);
Texture2D<float4> lx_runtimeEffect13 : register(t152);
Texture2D<float4> lx_runtimeBackgroundColor : register(t153);
Texture2D<float> lx_runtimeBackgroundDepth : register(t154);
SamplerState lx_runtimeReferenceSampler : register(s2);
#define gEnvironmentSampler lx_runtimeReferenceSampler
#include "PrincipledEnvironmentBake.slang"
#undef gEnvironmentSampler
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
#define LX_SCENE_REFRACTION_VOLUME 1
#define lx_inverseViewProjection lx_effectInverseViewProjection
#define lx_steps lx_effectSteps
#define lx_rays lx_effectRays
#define lx_reserved lx_effectBackgroundHasMedium
#define lx_depth lx_runtimeBackgroundDepth
#define lx_background lx_runtimeBackgroundColor
#define lx_environment lx_runtimeEnvironment
#include "MaterialGraphRefractionTransport.slang"
#undef lx_inverseViewProjection
#undef lx_steps
#undef lx_rays
#undef lx_reserved
#undef lx_depth
#undef lx_background
#undef lx_environment
#undef LX_SCENE_REFRACTION_VOLUME
#endif
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
StructuredBuffer<LXSceneRefractionSample> lx_refraction : register(t8);
#endif

struct LXSceneVertex
{
    float4 clip : SV_Position;
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 bitangent : TEXCOORD3;
    float2 uv : TEXCOORD4;
    nointerpolation uint owner : TEXCOORD5;
    nointerpolation uint reconstruction : TEXCOORD15;
    // 0 is the ordered/direct ABI; packet dispatches carry a frame record ID.
    nointerpolation uint record : TEXCOORD14;
    float4 previousClip : TEXCOORD6;
};

LXSceneVertex LXSceneReconstruct(float4 pixel, uint2 identity, out bool frontFace, bool primitiveCache = false);
LXSceneVertex LXSceneReconstructAtSlot(float4 pixel, uint2 identity, out bool frontFace, uint cacheSlot);
#include "MeteorVisibilityContract.slang"
Texture2D<uint2> lx_visibility : register(t159);
uint2 LXSceneLoadVisibility(uint2 pixel, uint record)
{
    const uint address = lx_visibility.Load(int3(pixel, 0)).x;
    const uint3 identity = LXSceneIdentity(record);
    const uint begin = identity.y, end = identity.z;
    if (address < begin || address >= end || begin == 0u) return uint2(0u);
    return uint2(identity.x, address - begin);
}

// Object-space stream point -> world space (position and PBR frame).
LXSurfacePoint LXSceneWorldPoint(LXSurfacePoint source, float4x4 world)
{
    return LXPrimitiveWorldPoint(source, world);
}

uint LXScenePrimitiveCacheSlot(uint2 identity, bool primitiveCache)
{
    // 기본 entry는 상수 false를 전달한다. record 조회와 GetDimensions까지 전부 DCE된다.
    if (!primitiveCache || identity.x == 0u)
    {
        return LXPrimitiveCacheMiss;
    }
    const uint3 range = LXSceneIdentity(identity.x);
    if (range.y == 0u || range.z <= range.y || identity.y >= range.z - range.y)
    {
        return LXPrimitiveCacheMiss;
    }
    return LXPrimitiveCacheFind(lx_primitiveCacheKeys, range.y + identity.y);
}

LXSceneVertex LXSceneVertexAt(uint index, uint owner, float4x4 viewProjection, float4x4 world, uint record = 0u)
{
    const LXSurfacePoint source = LXSceneWorldPoint(LXSceneGeometryPoint(record, index), world);
    LXSceneVertex output = (LXSceneVertex)0;
    output.clip = mul(float4(source.position.xyz, 1), viewProjection);
    output.position = source.position.xyz;
    output.normal = source.normal.xyz;
    output.tangent = source.tangent.xyz;
    output.bitangent = source.bitangent.xyz;
    output.uv = source.uvLod.xy;
    output.owner = owner;
    output.reconstruction = 0u;
    output.record = record;
    return output;
}

[shader("vertex")]
LXSceneVertex LXSceneVS(uint index : SV_VertexID, uint instanceId : SV_InstanceID)
{
    return LXSceneVertexAt(index, lx_visibleOwners[instanceId], lx_viewProjection, lx_world);
}

// Chunk-local topology over the same transformed vertices used by LXSceneVS.
// Scene preparation pins this immutable stream through submission completion.
#include "LxMeshletGpuContract.slang"

StructuredBuffer<uint> lx_shadowMeshletIds : register(t157);

// Per-primitive identity is emitted by the mesh shader, never inferred from
// compacted dispatch order or shared vertex interpolation.
struct LXScenePrimitive { uint address : SV_PrimitiveID; };

StructuredBuffer<uint> lx_rasterRoutes : register(t161);
RWStructuredBuffer<uint64_t> lx_visibilityKeys : register(u1);

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void LXSceneMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    out vertices LXSceneVertex vertices[64], out indices uint3 triangles[126],
    out primitives LXScenePrimitive primitives[126])
{
    const uint work = group.y * 65535u + group.x;
    const bool validWork = work < lx_shadowMeshletIds[0];
    const uint meshlet = lx_shadowMeshletIds[1u + (validWork ? work : 0u)];
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(lx_meshlets, meshlet);
    const uint enabled = uint(validWork) & uint(lx_visibleOwners[0] == lx_drawOwner);
    const uint vertexCount = descriptor.vertexCount * enabled;
    const uint triangleCount = descriptor.triangleCount * enabled;
    SetMeshOutputCounts(vertexCount, triangleCount);
    if (lane < vertexCount)
    {
        LXSceneVertex vertex = LXSceneVertexAt(LXMeshletVertex(lx_meshlets, descriptor, lane), lx_drawOwner, lx_viewProjection, lx_world);
        vertices[lane] = vertex;
    }
    for (uint triangle = lane; triangle < triangleCount; triangle += 64u)
    {
        triangles[triangle] = LXLoadGeometryPrimitive(lx_meshlets, descriptor, triangle).corners;
        primitives[triangle].address = (meshlet << 7u) | triangle;
    }
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void LXSceneVisibilityMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    out vertices LXSceneVertex vertices[64], out indices uint3 triangles[126],
    out primitives LXScenePrimitive primitives[126])
{
    const uint64_t flatWork = uint64_t(lx_packetSelector.z) + uint64_t(group.y) * lx_packetSelector.y + group.x;
    const uint work = uint(flatWork);
    const uint4 range = lx_scenePacketRanges[lx_packetSelector.x];
    const bool validWork = flatWork < range.y;
    uint record = 0u, meshlet = 0u, enabled = 0u;
    LXMeshletDescriptor descriptor = (LXMeshletDescriptor)0;
    if (validWork)
    {
        const uint2 packet = lx_scenePackets[range.x + work];
        record = packet.x;
        meshlet = packet.y;
        descriptor = LXSceneMeshlet(record, meshlet);
        enabled = uint(lx_rasterRoutes[(LXSceneIdentity(record).y - 1u) / 128u + meshlet] == 1u);
    }
    const uint vertexCount = descriptor.vertexCount * enabled;
    const uint triangleCount = descriptor.triangleCount * enabled;
    // A single straight-line output count avoids DXC duplicating the mesh intrinsic.
    SetMeshOutputCounts(vertexCount, triangleCount);
    if (lane < vertexCount)
    {
        LXSceneVertex vertex = LXSceneVertexAt(LXSceneMeshletVertex(record, descriptor, lane),
            LXSceneOwner(record), lx_viewProjection, LXSceneWorld(record), record);
        vertices[lane] = vertex;
    }
    for (uint triangle = lane; triangle < triangleCount; triangle += 64u)
    {
        triangles[triangle] = LXScenePrimitiveAt(record, descriptor, triangle).corners;
        primitives[triangle].address = (meshlet << 7u) | triangle;
    }
}

// Hardware coverage uses snapped subpixel vertices. Reconstruct attributes at
// the true pixel-center ray on the original triangle plane. Attribute and
// position derivatives span the same affine world-space barycentric plane;
// solving its homogeneous projection avoids extra index/vertex buffer reads.
// Coverage remains owned by the rasterizer. Visibility reprojects the restored
// position for its key; attachment passes retain native depth. Derivatives precede guards.
LXSceneVertex LXScenePixelCenter(LXSceneVertex input)
{
    if (input.reconstruction != 0u) return input;
    const float3 px=ddx_fine(input.position), py=ddy_fine(input.position);
    const float3 nx=ddx_fine(input.normal), ny=ddy_fine(input.normal);
    const float3 tx=ddx_fine(input.tangent), ty=ddy_fine(input.tangent);
    const float3 bx=ddx_fine(input.bitangent), by=ddy_fine(input.bitangent);
    const float2 ux=ddx_fine(input.uv), uy=ddy_fine(input.uv);
    const float4 c=mul(float4(input.position,1),lx_viewProjection);
    const float4 cx=mul(float4(px,0),lx_viewProjection), cy=mul(float4(py,0),lx_viewProjection);
    const float2 target=input.clip.xy/float2(lx_width,lx_height)*float2(2,-2)+float2(-1,1);
    const float2 x=cx.xy-target*cx.w, y=cy.xy-target*cy.w;
    const float2 delta=target*c.w-c.xy;
    const float determinant=x.x*y.y-x.y*y.x;
    if (isfinite(determinant) && abs(determinant)>1e-12)
    {
        const float2 step=float2(delta.x*y.y-delta.y*y.x,x.x*delta.y-x.y*delta.x)/determinant;
        if (all(isfinite(step)))
        {
            input.position+=px*step.x+py*step.y;
            input.normal+=nx*step.x+ny*step.y;
            input.tangent+=tx*step.x+ty*step.y;
            input.bitangent+=bx*step.x+by*step.y;
            input.uv+=ux*step.x+uy*step.y;
        }
    }
    return input;
}

cbuffer LXSceneShadowConstants : register(b4)
{
    float4x4 lx_shadowViewProjection;
    uint lx_shadowCoverage;
    float lx_shadowCutoff;
    uint lx_shadowPadding0;
    uint lx_shadowPadding;
    uint2 lx_shadowGeometryKey;
    uint2 lx_shadowGeometryObjectId;
    uint2 lx_shadowGeometryIncarnation;
    uint2 lx_shadowGeometryInstanceId;
    uint2 lx_shadowGeometryMaterialSlot;
    uint lx_shadowGeometryLodIndex;
    uint lx_shadowGeometryPrimitiveCount;
    uint lx_shadowGeometryIdentityVersion;
    uint3 lx_shadowGeometryIdentityPadding;
    float4x4 lx_shadowWorld;
};


[shader("vertex")]
LXSceneVertex LXSceneShadowVS(uint index : SV_VertexID)
{
    return LXSceneVertexAt(index, 0u, lx_shadowViewProjection, lx_shadowWorld);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void LXSceneShadowMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    out vertices LXSceneVertex vertices[64], out indices uint3 triangles[126])
{
    const uint64_t flatWork = uint64_t(lx_packetSelector.z) + uint64_t(group.y) * lx_packetSelector.y + group.x;
    const uint work = uint(flatWork);
    const uint4 range = lx_scenePacketRanges[lx_packetSelector.x];
    const bool validWork = flatWork < range.y;
    uint record = 0u;
    LXMeshletDescriptor descriptor = (LXMeshletDescriptor)0;
    if (validWork)
    {
        const uint2 packet = lx_scenePackets[range.x + work];
        record = packet.x;
        descriptor = LXSceneMeshlet(record, packet.y);
    }
    const uint enabled = uint(validWork);
    const uint vertexCount = descriptor.vertexCount * enabled;
    const uint triangleCount = descriptor.triangleCount * enabled;
    SetMeshOutputCounts(vertexCount, triangleCount);
    if (lane < vertexCount)
    {
        vertices[lane] = LXSceneVertexAt(LXSceneMeshletVertex(record, descriptor, lane), 0u,
            lx_shadowViewProjection, LXSceneWorld(record), record);
    }
    for (uint triangle = lane; triangle < triangleCount; triangle += 64u)
    {
        triangles[triangle] = LXScenePrimitiveAt(record, descriptor, triangle).corners;
    }
}

[shader("fragment")]
void LXSceneShadowPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXSceneGenerateMaterial(context, input.record);
    ApplyPbrCoverage(saturate(generated.surfaceInputs.alpha),
        input.record != 0u ? LXSceneCutoff(input.record) : lx_shadowCutoff,
        input.record != 0u ? LXSceneCoverage(input.record) : lx_shadowCoverage, frontFace);
}

MaterialInputs LXSceneDecalInputs(MaterialInputs raw, LXSceneVertex input, bool frontFace)
{
    if (lx_decalEnabled == 0u)
        return raw;
    const int3 pixel = int3(uint2(input.clip.xy), 0);
    const float3 color = lx_decalColor.Load(pixel).rgb;
    const float3 beforeColor = lx_beforeDecalColor.Load(pixel).rgb;
    const float3 orm = lx_decalOrm.Load(pixel).rgb;
    const float3 beforeOrm = lx_beforeDecalOrm.Load(pixel).rgb;
    // Compare stored representations, not raw graph values: no-op decals and
    // untouched channels must not introduce half-float rounding into lighting.
    raw.baseColor = select(color != beforeColor, color, raw.baseColor);
    raw.occlusion = orm.x != beforeOrm.x ? orm.x : raw.occlusion;
    raw.roughness = orm.y != beforeOrm.y ? orm.y : raw.roughness;
    raw.metallic = orm.z != beforeOrm.z ? orm.z : raw.metallic;
    const float3 normal = lx_decalNormal.Load(pixel).rgb;
    if (any(normal != lx_beforeDecalNormal.Load(pixel).rgb))
    {
        raw.normal = PrincipledUnitNormal(normal * 2.0 - 1.0, raw.normal);
        if ((LXSceneCoverage(input.record) & PBR_COVERAGE_DOUBLE_SIDED) != 0u && !frontFace)
            raw.normal = -raw.normal;
    }
    return raw;
}

PrincipledIblBakePoint LXScenePoint(LXSceneVertex input, bool frontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXSceneGenerateMaterial(context, input.record);
    const MaterialInputs raw = LXSceneDecalInputs(generated.surfaceInputs, input, frontFace);
    const float3 normal = PrincipledUnitNormal(raw.normal, context.normal);
    // Preserve authored IOR/level and the unrotated tangent. The compute baker
    // applies material evaluation once, exactly as the evaluated-point path.
    PrincipledIblBakePoint point;
    point.baseAlpha = float4(raw.baseColor, input.reconstruction != 0u ? 1.0 : ApplyPbrCoverage(saturate(raw.alpha), LXSceneCutoff(input.record), LXSceneCoverage(input.record), frontFace));
    point.normalRoughness = float4(OrientPbrNormal(normal, LXSceneCoverage(input.record), frontFace), raw.roughness);
    point.metalIorLevelAo = float4(raw.metallic, raw.ior, raw.specularIorLevel, raw.occlusion);
    point.tintAnisotropy = float4(raw.specularTint, raw.anisotropy);
    point.emissionColorStrength = float4(raw.emissionColor, raw.emissionStrength);
    point.coatWeightRoughIorFilmThickness = float4(raw.coatWeight, raw.coatRoughness, raw.coatIor, raw.thinFilmThickness);
    point.coatTintFilmIor = float4(raw.coatTint, raw.thinFilmIor);
    point.coatNormalSheenWeight = float4(OrientPbrNormal(PrincipledUnitNormal(raw.coatNormal, context.normal),
                                                      LXSceneCoverage(input.record), frontFace), raw.sheenWeight);
    point.sheenTintRoughness = float4(raw.sheenTint, raw.sheenRoughness);
    const float3 tangent = PrincipledUnitNormal(raw.tangent - normal * dot(raw.tangent, normal),
                                               PrincipledProjectedTangent(context.tangent, normal));
    const bool flipped = (LXSceneCoverage(input.record) & PBR_COVERAGE_DOUBLE_SIDED) != 0u && !frontFace;
    point.tangentRotation = float4(tangent, flipped ? -raw.anisotropyRotation : raw.anisotropyRotation);
    point.viewTier = float4(PrincipledUnitNormal(lx_eye.xyz - input.position, normal),
                            (LX_MATERIAL_FEATURE_MASK & 0x17C0u) != 0 ? 1 : 0);
    // Principled core graphs use the same GGX closures with film/coat disabled.
    // This lookup tag describes the evaluation model, not the authoring tier.
    // Fine UV derivatives and graph evaluation precede this divergent owner test.
    if (input.reconstruction == 0u && LXSceneBlended(input.record) == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != LXSceneOwner(input.record))
        discard;
    return point;
}

PrincipledIblBakeSample LXSceneReadIbl(LXSceneVertex input, bool frontFace, uint index)
{
    if (lx_runtimeLookup == 0u)
    {
        return lx_ibl[index];
    }
    // capture와 동일한 raw graph 값/접선/양면 처리를 사용한다. 반정밀도
    // GBuffer를 역변환하거나 화면 크기의 bake point를 유지하지 않는다.
    const PrincipledIblBakePoint point = LXScenePoint(input, frontFace);
    if (!IsValidScenePrincipledIblBakePoint(point))
    {
        PrincipledIblBakeSample invalid = (PrincipledIblBakeSample)0;
        invalid.baseAverage.w = -1;
        return invalid;
    }
    return LXSplitSumSample(EvaluateIblBakePoint(point), normalize(point.viewTier.xyz), point.viewTier.w == 1.0);
}

struct LXSceneLookup0
{
    float4 fields[8] : SV_Target;
};

struct LXSceneLookup1
{
    float4 fields[3] : SV_Target;
};

LXSceneLookup0 LXSceneLookup0PSEvaluate(LXSceneVertex input, bool frontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledIblBakePoint point = LXScenePoint(input, frontFace);
    LXSceneLookup0 output;
    output.fields[0] = point.baseAlpha;
    output.fields[1] = point.normalRoughness;
    output.fields[2] = point.metalIorLevelAo;
    output.fields[3] = point.tintAnisotropy;
    output.fields[4] = point.emissionColorStrength;
    output.fields[5] = point.coatWeightRoughIorFilmThickness;
    output.fields[6] = point.coatTintFilmIor;
    output.fields[7] = point.coatNormalSheenWeight;
    return output;
}
[shader("fragment")]
LXSceneLookup0 LXSceneLookup0PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    if (input.reconstruction == 0u) return LXSceneLookup0PSEvaluate(input, frontFace);
    const uint2 own = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
    LXSceneLookup0 output = (LXSceneLookup0)0;
    [loop] for (uint lane = 0u; lane < 4u; ++lane) {
        const uint2 identity = QuadReadLaneAt(own, lane);
        if (identity.x == LXSceneIdentity(input.record).x) {
            bool face;
            const LXSceneVertex restored = LXSceneReconstruct(input.clip, identity, face);
            const LXSceneLookup0 value = LXSceneLookup0PSEvaluate(restored, face);
            if (all(own == identity)) output = value;
        }
    }
    if (own.x != LXSceneIdentity(input.record).x) discard;
    return output;
}

LXSceneLookup1 LXSceneLookup1PSEvaluate(LXSceneVertex input, bool frontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledIblBakePoint point = LXScenePoint(input, frontFace);
    LXSceneLookup1 output;
    output.fields[0] = point.sheenTintRoughness;
    output.fields[1] = point.tangentRotation;
    output.fields[2] = point.viewTier;
    return output;
}
[shader("fragment")]
LXSceneLookup1 LXSceneLookup1PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    if (input.reconstruction == 0u) return LXSceneLookup1PSEvaluate(input, frontFace);
    const uint2 own = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
    LXSceneLookup1 output = (LXSceneLookup1)0;
    [loop] for (uint lane = 0u; lane < 4u; ++lane) {
        const uint2 identity = QuadReadLaneAt(own, lane);
        if (identity.x == LXSceneIdentity(input.record).x) {
            bool face;
            const LXSceneVertex restored = LXSceneReconstruct(input.clip, identity, face);
            const LXSceneLookup1 value = LXSceneLookup1PSEvaluate(restored, face);
            if (all(own == identity)) output = value;
        }
    }
    if (own.x != LXSceneIdentity(input.record).x) discard;
    return output;
}

LXMaterialContext LXSceneContext(LXSceneVertex input)
{
    LXMaterialContext context;
    context.uv = float3(input.uv, 0);
    context.normal = PrincipledUnitNormal(input.normal, float3(0, 0, 1));
    context.tangent = PrincipledUnitNormal(input.tangent, float3(1, 0, 0));
    context.bitangent = PrincipledUnitNormal(input.bitangent, cross(context.normal, context.tangent));
    // Scene image operations derive LOD from each final Vector expression and
    // image dimensions. Explicit LOD remains available to compute/point hosts.
    context.lod = 0.0;
    return context;
}

PrincipledSurface LXSceneEvaluateMaterial(LXGeneratedMaterial generated, LXMaterialContext context, bool frontFace, bool reconstructed, uint record)
{
    PrincipledSurface surface = LXEvaluateGeneratedSurface(generated, context);
    surface.alpha = reconstructed ? 1.0 : ApplyPbrCoverage(surface.alpha, LXSceneCutoff(record), LXSceneCoverage(record), frontFace);
    surface.normal = OrientPbrNormal(surface.normal, LXSceneCoverage(record), frontFace);
    surface.coatNormal = OrientPbrNormal(surface.coatNormal, LXSceneCoverage(record), frontFace);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    if (!frontFace)
    {
        // Coverage already oriented both normals. Only the medium ratio remains.
        surface.transmissionFilmIor /= surface.transmissionIor;
        surface.transmissionIor = rcp(surface.transmissionIor);
    }
#endif
    return surface;
}

// The GBuffer precedes Decal. Keep its call graph free of Decal descriptors;
// a runtime boolean inside a shared function still exposes those resources in SPIR-V.
PrincipledSurface LXSceneUndecaledMaterial(LXSceneVertex input, bool frontFace)
{
#if defined(CE_EDITOR_MINIMAL_PREVIEW)
    // Model-space procedural checker: no UV/texture/material/IBL evaluation.
    // Invert the orthogonal TRS basis, retaining non-uniform object scale.
    const float4x4 world = LXSceneWorld(input.record);
    const float3 delta = input.position - world[3].xyz;
    const float3 local = float3(dot(delta, world[0].xyz) / max(dot(world[0].xyz, world[0].xyz), 1e-12),
        dot(delta, world[1].xyz) / max(dot(world[1].xyz, world[1].xyz), 1e-12),
        dot(delta, world[2].xyz) / max(dot(world[2].xyz, world[2].xyz), 1e-12));
    const float3 grid = local * 6.0;
    const float parity = frac(dot(floor(grid), float3(1, 1, 1)) * 0.5) * 2.0;
    const float3 footprint = fwidth(grid);
    const float checker = lerp(0.5, parity, saturate(1.0 - max(footprint.x, max(footprint.y, footprint.z))));
    PrincipledSurface surface = (PrincipledSurface)0;
    surface.normal = PrincipledUnitNormal(input.normal, float3(0, 1, 0));
    const float shade = 0.3 + 0.7 * abs(dot(surface.normal, normalize(float3(0.4, 0.8, -0.5))));
    surface.emission = lerp(float3(0.12), float3(0.65), checker) * shade;
    surface.alpha = 1.0;
    surface.roughness = 1.0;
    surface.occlusion = 1.0;
    return surface;
#else
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXSceneGenerateMaterial(context, input.record);
    return LXSceneEvaluateMaterial(generated, context, frontFace, input.reconstruction != 0u, input.record);
#endif
}

PrincipledSurface LXSceneMaterial(LXSceneVertex input, bool frontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    LXGeneratedMaterial generated = LXSceneGenerateMaterial(context, input.record);
    generated.surfaceInputs = LXSceneDecalInputs(generated.surfaceInputs, input, frontFace);
    return LXSceneEvaluateMaterial(generated, context, frontFace, input.reconstruction != 0u, input.record);
}

#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
struct LXSceneRefractionCapture
{
    float4 positionIor : SV_Target0;
    float4 normalFilmIor : SV_Target1;
};

[shader("fragment")]
LXSceneRefractionCapture LXSceneRefractionPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledSurface material = LXSceneMaterial(input, frontFace);
    if (input.reconstruction == 0u && LXSceneBlended(input.record) == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != LXSceneOwner(input.record))
    {
        discard;
    }
    LXSceneRefractionCapture output;
    output.positionIor = float4(input.position, material.transmissionIor);
    output.normalFilmIor = float4(material.normal, material.transmissionFilmIor);
    return output;
}
#endif

struct LXSceneGBuffer
{
    float4 diffuse : SV_Target0;
    float4 metalRough : SV_Target1;
    float4 normal : SV_Target2;
    float4 emissive : SV_Target3;
    uint owner : SV_Target4;
};

LXSceneGBuffer LXSceneEvaluateSurface(LXSceneVertex input, bool frontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledSurface surface = LXSceneUndecaledMaterial(input, frontFace);
    if (LXSceneBlended(input.record) == 1u && surface.alpha <= 0.0) discard;
    LXSceneGBuffer output;
    output.diffuse = float4(surface.baseColor, surface.alpha);
    output.metalRough = float4(surface.occlusion, surface.roughness, surface.metallic, 1);
    output.normal = float4(surface.normal * 0.5 + 0.5, 1);
    output.emissive = float4(surface.emission, 1);
    output.owner = input.owner;
    return output;
}

LXSceneGBuffer LXSceneGBufferPSEvaluate(LXSceneVertex input, bool frontFace)
{
    return LXSceneEvaluateSurface(input, frontFace);
}
// The mask is uniform within a quad: every lane observes the same four IDs.
// Keep the first occurrence of each complete {record, primitive} identity.
// Every helper lane must still reconstruct/evaluate each selected primitive at
// its own pixel center so material derivatives remain valid at triangle edges.
uint LXSceneUniqueQuadIdentityLanes(uint2 own)
{
    const uint2 q0 = QuadReadLaneAt(own, 0u);
    const uint2 q1 = QuadReadLaneAt(own, 1u);
    const uint2 q2 = QuadReadLaneAt(own, 2u);
    const uint2 q3 = QuadReadLaneAt(own, 3u);
    return 1u |
        (uint(any(q1 != q0)) << 1u) |
        (uint(any(q2 != q0) && any(q2 != q1)) << 2u) |
        (uint(any(q3 != q0) && any(q3 != q1) && any(q3 != q2)) << 3u);
}
LXSceneGBuffer LXSceneGBufferEvaluate(LXSceneVertex input, bool frontFace, bool primitiveCache)
{
    if (input.reconstruction == 0u) return LXSceneGBufferPSEvaluate(input, frontFace);
    const uint2 own = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
    const uint uniqueLanes = LXSceneUniqueQuadIdentityLanes(own);
    LXSceneGBuffer output = (LXSceneGBuffer)0;
    [loop] for (uint lane = 0u; lane < 4u; ++lane) {
        const uint2 identity = QuadReadLaneAt(own, lane);
        if ((uniqueLanes & (1u << lane)) != 0u && identity.x == LXSceneIdentity(input.record).x)
        {
            bool face;
            const LXSceneVertex restored = LXSceneReconstruct(input.clip, identity, face, primitiveCache);
            const LXSceneGBuffer value = LXSceneGBufferPSEvaluate(restored, face);
            if (all(own == identity)) output = value;
        }
    }
    if (own.x != LXSceneIdentity(input.record).x) discard;
    return output;
}

[shader("fragment")]
LXSceneGBuffer LXSceneGBufferPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    return LXSceneGBufferEvaluate(input, frontFace, false);
}

[shader("fragment")]
LXSceneGBuffer LXSceneCachedGBufferPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    return LXSceneGBufferEvaluate(input, frontFace, true);
}

StructuredBuffer<LXFrameGeometryRecord> lx_frameGeometry : register(t158);


// Opaque coverage/depth is decided only here. Material attributes are evaluated
// by the reconstruction consumer, not written as additional raster MRTs.
[shader("fragment")]
void LXSceneVisibilityPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace,
    uint primitiveAddress : SV_PrimitiveID)
{
    input = LXScenePixelCenter(input);
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXSceneGenerateMaterial(context, input.record);
    ApplyPbrCoverage(saturate(generated.surfaceInputs.alpha), LXSceneCutoff(input.record), LXSceneCoverage(input.record), frontFace);
    // Visibility stores the original triangle plane at the true pixel center.
    // LXScenePixelCenter already restores that position from the snapped raster
    // interpolation. Reproject it instead of mixing that position with the
    // rasterizer's independent SV_Position depth plane in the atomic key.
    const float4 centerClip = mul(float4(input.position, 1.0f), lx_viewProjection);
    const float depth = centerClip.z / centerClip.w;
    if (isfinite(depth) && depth >= 0.0f && depth < 1.0f)
    {
        const uint2 pixel = uint2(input.clip.xy);
        uint64_t ignored;
        InterlockedMin(lx_visibilityKeys[pixel.y * lx_width + pixel.x],
            MeteorVisibilityKey(depth, LXSceneIdentity(input.record).y + primitiveAddress), ignored);
    }
}

// One mesh group consumes one record/tile packet from the selected PSO bucket.
[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void LXSceneReconstructMS(uint lane : SV_GroupIndex, uint3 group : SV_GroupID,
    out vertices LXSceneVertex vertices[4], out indices uint3 triangles[2],
    out primitives LXScenePrimitive primitives[2])
{
    const uint64_t flatWork = uint64_t(lx_packetSelector.z) + group.x + uint64_t(group.y) * lx_packetSelector.y;
    const uint index = uint(flatWork);
    const uint4 range = lx_scenePacketRanges[lx_packetSelector.x];
    const bool valid = flatWork < range.y;
    SetMeshOutputCounts(4u * uint(valid), 2u * uint(valid));
    if (!valid) return;
    const uint2 packet = lx_scenePackets[range.x + index];
    const uint record = packet.x, tile = packet.y;
    const uint grid = (lx_width + 7u) / 8u;
    const uint2 origin = uint2(tile % grid, tile / grid) * 8u;
    if (lane < 4u) {
        LXSceneVertex v = (LXSceneVertex)0;
        const float2 pixel = min(origin + uint2(lane & 1u, lane >> 1u) * 8u, uint2(lx_width,lx_height));
        v.clip = float4(pixel / float2(lx_width,lx_height) * float2(2,-2) + float2(-1,1),0,1);
        v.reconstruction = 1u;
        v.record = record;
        v.owner = LXSceneOwner(record);
        vertices[lane] = v;
    }
    if (lane == 0u) { triangles[0] = uint3(0,1,2); triangles[1] = uint3(2,1,3); primitives[0].address=0; primitives[1].address=0; }
}

LXSceneVertex LXSceneReconstruct(float4 pixel, uint2 identity, out bool frontFace, bool primitiveCache)
{
    return LXSceneReconstructAtSlot(pixel, identity, frontFace, LXScenePrimitiveCacheSlot(identity, primitiveCache));
}

LXSceneVertex LXSceneReconstructAtSlot(float4 pixel, uint2 identity, out bool frontFace, uint cacheSlot)
{
    const uint record = identity.x;
    LXSurfacePoint a, b, c;
    float4 ca, cb, cc;
    if (cacheSlot != LXPrimitiveCacheMiss)
    {
        const LXPrimitiveCacheTriangle cached = lx_primitiveCacheData[cacheSlot];
        a = cached.a.point;
        b = cached.b.point;
        c = cached.c.point;
        ca = cached.a.rasterClip;
        cb = cached.b.rasterClip;
        cc = cached.c.rasterClip;
    }
    else
    {
        const LXMeshletDescriptor descriptor = LXSceneMeshlet(record, identity.y >> 7u);
        const LXGeometryPrimitive primitive = LXScenePrimitiveAt(record, descriptor, identity.y & 127u);
        const float4x4 world = LXSceneWorld(record);
        a = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.x), world);
        b = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.y), world);
        c = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.z), world);
        ca = mul(float4(a.position.xyz, 1), lx_viewProjection);
        cb = mul(float4(b.position.xyz, 1), lx_viewProjection);
        cc = mul(float4(c.position.xyz, 1), lx_viewProjection);
    }
    // Homogeneous solve also handles a triangle crossing the near plane. Do not
    // divide vertices by w: clipped vertices can have zero/negative w.
    const float2 ndc = pixel.xy / float2(lx_width, lx_height) * float2(2, -2) + float2(-1, 1);
    const float2 x = cb.xy - ca.xy - ndc * (cb.w - ca.w);
    const float2 y = cc.xy - ca.xy - ndc * (cc.w - ca.w);
    const float2 delta = ndc * ca.w - ca.xy;
    const float determinant = x.x * y.y - x.y * y.x;
    const float2 weights = float2(delta.x * y.y - delta.y * y.x,
        x.x * delta.y - x.y * delta.x) / determinant;
    const float3 w = float3(1.0 - weights.x - weights.y, weights);
    LXSceneVertex v = (LXSceneVertex)0;
    v.clip = pixel;
    v.position = a.position.xyz * w.x + b.position.xyz * w.y + c.position.xyz * w.z;
    v.normal = a.normal.xyz * w.x + b.normal.xyz * w.y + c.normal.xyz * w.z;
    v.tangent = a.tangent.xyz * w.x + b.tangent.xyz * w.y + c.tangent.xyz * w.z;
    v.bitangent = a.bitangent.xyz * w.x + b.bitangent.xyz * w.y + c.bitangent.xyz * w.z;
    v.uv = a.uvLod.xy * w.x + b.uvLod.xy * w.y + c.uvLod.xy * w.z;
    v.owner = LXSceneOwner(record);
    v.reconstruction = 1u;
    v.record = record;
    frontFace = determinant < 0.0;
    return v;
}

// Current/previous posed streams share topology; worlds remain instance inputs.
[shader("vertex")]
LXSceneVertex LXSceneTemporalVS(uint index : SV_VertexID, uint instanceId : SV_InstanceID)
{
    LXSceneVertex output = LXSceneVertexAt(index, lx_visibleOwners[instanceId], lx_viewProjection, lx_world);
    const float3 previous = mul(float4(MeteorGeometryPoint(lx_previousVertices,lx_meshlets,index).position.xyz, 1), lx_previousWorld).xyz;
    output.previousClip = mul(float4(previous, 1), lx_temporalPreviousProjection);
    return output;
}
struct LXTemporalOutput
{
    float2 motion : SV_Target0;
    float reactive : SV_Target1;
    float transparency : SV_Target2;
    float responsive : SV_Target3;
    float depth : SV_Depth;
};
LXTemporalOutput LXSceneTemporalEvaluate(LXSceneVertex input, bool frontFace, uint primitiveAddress,
                                         bool primitiveCache)
{
    const bool reconstructed = input.reconstruction != 0u;
    LXSceneVertex centered;
    uint address = primitiveAddress;
    uint cacheSlot = LXPrimitiveCacheMiss;
    if (reconstructed) {
        const uint2 identity = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
        if (identity.x == 0u) discard;
        cacheSlot = LXScenePrimitiveCacheSlot(identity, primitiveCache);
        centered = LXSceneReconstructAtSlot(input.clip, identity, frontFace, cacheSlot);
        address = identity.y;
    } else centered = LXScenePixelCenter(input);
    // The selected current topology also indexes the retained previous pose.
    // A LOD transition never reads the previous frame's different topology.
    const uint record = centered.record;
    float3 a, b, c, previousA, previousB, previousC;
    if (cacheSlot != LXPrimitiveCacheMiss)
    {
        const LXPrimitiveCacheTriangle cached = lx_primitiveCacheData[cacheSlot];
        a = cached.a.point.position.xyz;
        b = cached.b.point.position.xyz;
        c = cached.c.point.position.xyz;
        previousA = cached.a.previousPosition.xyz;
        previousB = cached.b.previousPosition.xyz;
        previousC = cached.c.previousPosition.xyz;
    }
    else
    {
        const LXMeshletDescriptor descriptor = LXSceneMeshlet(record, address >> 7u);
        const LXGeometryPrimitive primitive = LXScenePrimitiveAt(record, descriptor, address & 127u);
        const float4x4 world = LXSceneWorld(record);
        a = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.x), world).position.xyz;
        b = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.y), world).position.xyz;
        c = LXSceneWorldPoint(LXSceneGeometryPoint(record, primitive.vertices.z), world).position.xyz;
        previousA = LXSceneGeometryPoint(record, primitive.vertices.x, true).position.xyz;
        previousB = LXSceneGeometryPoint(record, primitive.vertices.y, true).position.xyz;
        previousC = LXSceneGeometryPoint(record, primitive.vertices.z, true).position.xyz;
    }
    const float3 x=b-a, y=c-a, delta=centered.position-a;
    const float xx=dot(x,x), xy=dot(x,y), yy=dot(y,y), determinant=xx*yy-xy*xy;
    const float2 uv=float2(dot(delta,x)*yy-dot(delta,y)*xy,dot(delta,y)*xx-dot(delta,x)*xy)/determinant;
    const float3 weights=float3(1-uv.x-uv.y,uv);
    const float3 previous = previousA * weights.x + previousB * weights.y + previousC * weights.z;
    const float4 previousClip=mul(mul(float4(previous,1),LXSceneWorld(record, true)),lx_temporalPreviousProjection);
    const float4 currentClip=mul(float4(centered.position,1),lx_temporalCurrentProjection);
    // Opaque coverage is already fixed by final visibility; only replay evaluates alpha.
    float alpha=1;
    if(!reconstructed) alpha=LXSceneUndecaledMaterial(centered, frontFace).alpha;
    if (LXSceneBlended(input.record) == 1u && alpha <= 0.01) discard;
    LXTemporalOutput output;
    const bool valid = LXSceneHistoryValid(input.record) != 0u && currentClip.w > 0.000001 &&
        previousClip.w > 0.000001 && all(isfinite(previousClip));
    output.motion = valid ? (previousClip.xy / previousClip.w -
        currentClip.xy / currentClip.w) * float2(0.5 * lx_width, -0.5 * lx_height) : float2(0);
    output.transparency = LXSceneBlended(input.record) != 0u ? saturate(alpha) : 0;
    output.reactive = LXSceneBlended(input.record) != 0u ? 1 : 0;
    output.responsive = valid ? 0 : 1;
    const float4 rasterClip=mul(float4(centered.position,1),lx_viewProjection);
    output.depth=rasterClip.z/rasterClip.w;
    return output;
}

[shader("fragment")]
LXTemporalOutput LXSceneTemporalPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace,
                                   uint primitiveAddress : SV_PrimitiveID)
{
    return LXSceneTemporalEvaluate(input, frontFace, primitiveAddress, false);
}

[shader("fragment")]
LXTemporalOutput LXSceneCachedTemporalPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace,
                                         uint primitiveAddress : SV_PrimitiveID)
{
    return LXSceneTemporalEvaluate(input, frontFace, primitiveAddress, true);
}

// Shared cascaded-shadow evaluation for METEOR material packets and layer shading.
CascadedShadowView LXSceneShadowView()
{
    CascadedShadowView view;
    view.lightViewProjection = lx_shadowProjection;
    view.eye = lx_eye.xyz;
    view.cameraForward = lx_cameraForward;
    view.splits = lx_shadowSplits;
    view.bias = lx_shadowBias;
    view.blendBand = lx_shadowBlend;
    return view;
}

float LXSceneShadow(float3 position, float3 normal, float3 light)
{
    if (lx_hasShadow == 0u)
        return 1.0;
    return CascadedShadowVisibility(lx_shadow, lx_shadowSampler, LXSceneShadowView(), position, normal, light);
}

// 그림자 디버그 보기의 대상인가. 켜져 있으면 고른 방향광을 돌려준다.
bool LXSceneShadowDebugLight(out uint mode, out LXSceneLight light, out float3 toLight)
{
    const CascadedShadowView view = LXSceneShadowView();
    mode = CascadedShadowDebugView(view);
    const uint owner = CascadedShadowOwner(view);
    light = (LXSceneLight)0;
    toLight = float3(0, 1, 0);
    if (mode == CASCADED_SHADOW_DEBUG_OFF || lx_hasShadow == 0u || owner >= lx_lightCount)
        return false;
    light = LXSceneReadLight(owner);
    toLight = -normalize(light.direction.xyz);
    return true;
}

float3 LXSceneShadowDebugColor(uint mode, float3 position, float3 normal, float3 toLight, float3 lit, float3 selected)
{
    return CascadedShadowDebugColor(lx_shadow, lx_shadowSampler, LXSceneShadowView(), mode,
        position, normal, toLight, lit, selected);
}

#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0
LXSceneRuntimeEffectSample LXSceneLoadRuntimeSource(uint index)
{
    const uint2 pixel = uint2(index % lx_width, index / lx_width);
    const int3 local = int3(pixel - uint2(lx_effectSourceX, lx_effectSourceY), 0);
    LXSceneRuntimeEffectSample sample = (LXSceneRuntimeEffectSample)0;
    sample.sourceArea = lx_runtimeEffect0.Load(local);
    sample.positionValid = lx_runtimeEffect1.Load(local);
    sample.normal = lx_runtimeEffect2.Load(local).xyz;
    sample.owner = lx_owners.Load(int3(pixel, 0));
    return sample;
}

LXSceneRuntimeEffectSample LXSceneLoadRuntimeEffect(uint index)
{
    const uint2 pixel = uint2(index % lx_width, index / lx_width);
    const int3 local = int3(pixel - uint2(lx_effectSourceX, lx_effectSourceY), 0);
    LXSceneRuntimeEffectSample sample = LXSceneLoadRuntimeSource(index);
    sample.reflection.metalSingleAlbedo = lx_runtimeEffect3.Load(local);
    sample.reflection.metalAverage = lx_runtimeEffect4.Load(local);
    sample.reflection.dielectricSingleAlbedo = lx_runtimeEffect5.Load(local);
    sample.reflection.dielectricAverage = lx_runtimeEffect6.Load(local);
    sample.reflection.metalEnvironment = lx_runtimeEffect7.Load(local);
    sample.reflection.dielectricEnvironment = lx_runtimeEffect8.Load(local);
    sample.refraction.glassSingleAlbedo = lx_runtimeEffect9.Load(local);
    sample.refraction.glassAverage = lx_runtimeEffect10.Load(local);
    sample.refraction.glassEnvironment = lx_runtimeEffect11.Load(local);
    sample.refraction.rawTransmission = lx_runtimeEffect12.Load(local);
    sample.refraction.radiance = lx_runtimeEffect13.Load(local);
    return sample;
}

LXSceneSubsurfaceReflection LXSceneReadReflection(uint index)
{
    if (lx_runtimeLookup != 0u)
    {
        return LXSceneLoadRuntimeEffect(index).reflection;
    }
    return lx_subsurfaceReflection[index];
}

LXSceneRefractionSample LXSceneReadRefraction(uint index)
{
    LXSceneRefractionSample sample = (LXSceneRefractionSample)0;
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    if (lx_runtimeLookup != 0u)
    {
        return LXSceneLoadRuntimeEffect(index).refraction;
    }
    return lx_refraction[index];
#else
    return sample;
#endif
}

SpecialForwardSurface LXSceneSpecialSurfaceFromSamples(PrincipledSurface material, float3 view,
    PrincipledIblBakeSample sample, LXSceneSubsurfaceReflection reflection, LXSceneRefractionSample refraction)
{
    SpecialForwardIntegral integral;
    integral.layers.base = ReadPrincipledIblIntegral(sample.baseSingleAlbedo, sample.baseAverage);
    integral.layers.coat = ReadPrincipledIblIntegral(sample.coatSingleAlbedo, sample.coatAverage);
    integral.metal = ReadPrincipledIblIntegral(reflection.metalSingleAlbedo, reflection.metalAverage);
    integral.dielectric = ReadPrincipledIblIntegral(reflection.dielectricSingleAlbedo, reflection.dielectricAverage);
    integral.glass = PrincipledEmptyIntegral();
    integral.rawTransmission = float3(0);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    integral.glass = ReadPrincipledIblIntegral(refraction.glassSingleAlbedo, refraction.glassAverage);
    integral.rawTransmission = refraction.rawTransmission.rgb;
#endif
    return BuildSpecialForwardSurface(material, view, integral);
}

SpecialForwardSurface LXSceneSpecialSurface(PrincipledSurface material, float3 view, uint index,
    PrincipledIblBakeSample sample)
{
    return LXSceneSpecialSurfaceFromSamples(material, view, sample, LXSceneReadReflection(index),
        LXSceneReadRefraction(index));
}

float3 LXSceneSpecialDirect(SpecialForwardSurface surface, float2 pixel, float3 position, out float3 irradiance)
{
    float3 direct = float3(0);
    irradiance = float3(0);
    uint tile;
    bool tiled;
    const uint count = LXSceneLightCount(pixel, tile, tiled);
    for (uint i = 0; i < count; ++i)
    {
        const uint lightIndex = tiled ? lx_forwardIndices[tile * 32u + i] : i;
        const LXSceneLight record = LXSceneReadLight(lightIndex);
        const float4 lightPosition = record.position;
        const float4 direction = record.direction;
        const float4 color = record.color;
        const float4 falloff = record.attenuation;
        const uint type = uint(lightPosition.w);
        float3 toLight;
        float attenuation;
        if (type == 0u)
        {
            toLight = -normalize(direction.xyz);
            attenuation = (lightIndex + 1u == uint(max(lx_shadowSplits.w, 1.0))) ? LXSceneShadow(position, surface.layers.material.normal, toLight) : 1.0;
        }
        else
        {
            const float3 offset = lightPosition.xyz - position;
            const float distance = length(offset);
            toLight = offset / max(distance, 1e-4);
            attenuation = saturate(1.0 - distance / max(falloff.w, 1e-4))
                / max(falloff.x + falloff.y * distance + falloff.z * distance * distance, 1e-6);
            if (type == 2u)
            {
                const float cosine = dot(-toLight, normalize(direction.xyz));
                const float cutoff = cos(direction.w * 0.5);
                attenuation *= smoothstep(cutoff, lerp(cutoff, 1.0, 0.2), cosine);
            }
        }
        const SpecialForwardLight light = EvaluateSpecialLight(surface, toLight, color.rgb * color.a * attenuation);
        direct += light.local + light.transmission;
        irradiance += light.subsurfaceSource;
    }
    return direct;
}

#include "MaterialGraphRuntimeSpecial.slang"

struct LXSceneSubsurfaceCapture
{
    float4 fields[7] : SV_Target;
};

LXSceneSubsurfaceCapture LXSceneSubsurfacePSEvaluate(LXSceneVertex input, bool frontFace)
{
    input=LXScenePixelCenter(input);
    // Evaluate quad derivatives before alpha/face coverage can discard a lane.
    const float area = length(cross(ddx_fine(input.position), ddy_fine(input.position)));
    const PrincipledSurface material = LXSceneMaterial(input, frontFace);
    const uint index = uint(input.clip.y) * lx_width + uint(input.clip.x);
    const PrincipledIblBakeSample sample = LXSceneReadIbl(input, frontFace, index);
    if (input.reconstruction == 0u && LXSceneBlended(input.record) == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != LXSceneOwner(input.record))
    {
        discard;
    }
    LXSceneSubsurfaceCapture output = (LXSceneSubsurfaceCapture)0;
    if (sample.baseAverage.w < 0 || lx_subsurfaceReflection[index].metalAverage.w != 1.0
        || material.subsurfaceWeight <= 0.0)
    {
        return output;
    }
    const float3 view = PrincipledUnitNormal(lx_eye.xyz - input.position, material.normal);
    const SpecialForwardSurface surface = LXSceneSpecialSurface(material, view, index, sample);
    float3 irradiance;
    LXSceneSpecialDirect(surface, input.clip.xy, input.position, irradiance);
    if (lx_hasEnvironment != 0u)
    {
        irradiance += sample.irradiance.rgb;
    }
    const PrincipledSubsurfaceProfile profile = BuildSceneSubsurfaceProfile(material);
    const uint localMask = (profile.local.x ? 1u : 0u) | (profile.local.y ? 2u : 0u)
        | (profile.local.z ? 4u : 0u);
    output.fields[0] = float4(irradiance, area);
    output.fields[1] = float4(input.position, 1);
    output.fields[2] = float4(material.normal, float(localMask));
    output.fields[3] = float4(profile.realDepth, 0);
    output.fields[4] = float4(profile.virtualDepth, 0);
    output.fields[5] = float4(profile.attenuation, 0);
    output.fields[6] = float4(profile.normalization, 0);
    return output;
}
[shader("fragment")]
LXSceneSubsurfaceCapture LXSceneSubsurfacePS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    if (input.reconstruction == 0u) return LXSceneSubsurfacePSEvaluate(input, frontFace);
    const uint2 own = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
    LXSceneSubsurfaceCapture output = (LXSceneSubsurfaceCapture)0;
    [loop] for (uint lane = 0u; lane < 4u; ++lane) {
        const uint2 identity = QuadReadLaneAt(own, lane);
        if (identity.x == LXSceneIdentity(input.record).x) {
            bool face;
            const LXSceneVertex restored = LXSceneReconstruct(input.clip, identity, face);
            const LXSceneSubsurfaceCapture value = LXSceneSubsurfacePSEvaluate(restored, face);
            if (all(own == identity)) output = value;
        }
    }
    if (own.x != LXSceneIdentity(input.record).x) discard;
    return output;
}

float4 LXSceneSpecialColor(PrincipledSurface material, float2 pixel, float3 position, uint index,
    PrincipledIblBakeSample sample, uint record)
{
    if (sample.baseAverage.w < 0 || LXSceneReadReflection(index).metalAverage.w != 1.0)
    {
        return float4(1, 0, 1, 1);
    }
    const float3 view = PrincipledUnitNormal(lx_eye.xyz - position, material.normal);
    const SpecialForwardSurface surface = LXSceneSpecialSurface(material, view, index, sample);
    float3 source;
    const float3 direct = LXSceneSpecialDirect(surface, pixel, position, source);
    const LXSceneSubsurfaceReflection reflection = LXSceneReadReflection(index);
    SpecialForwardEnvironment environment = (SpecialForwardEnvironment)0;
    environment.irradiance = sample.irradiance.rgb;
    environment.coatPrefiltered = sample.coatPrefiltered.rgb;
    environment.sheenIrradiance = sample.sheenIrradiance.rgb;
    const float3 metal = material.metallic * surface.integral.metal.singleScatter;
    const float3 dielectric = (1.0 - material.metallic) * (1.0 - material.transmissionWeight)
        * surface.integral.dielectric.singleScatter;
    environment.basePrefiltered = (metal * reflection.metalEnvironment.rgb
        + dielectric * reflection.dielectricEnvironment.rgb) / max(metal + dielectric, 1e-20);
    if (lx_runtimeLookup != 0u)
    {
        environment.subsurfaceIrradiance = LXSceneRuntimeSubsurface(material, index);
    }
    else
    {
        environment.subsurfaceIrradiance = lx_subsurfaceIrradiance[index].rgb;
    }
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    const LXSceneRefractionSample refraction = LXSceneReadRefraction(index);
    if (refraction.glassAverage.w != 1)
    {
        return float4(1, 0, 1, 1);
    }
    environment.glassPrefiltered = refraction.glassEnvironment.rgb;
    environment.refractedRadiance = refraction.radiance.rgb;
#endif
    SpecialForwardAmbient ambient = EvaluateSpecialIbl(surface, environment);
    // The prepared field already includes direct irradiance and per-channel
    // zero-radius local fallback. Apply destination color and weight once.
    ambient.subsurface = surface.layers.lowerTransmission * surface.subsurface
        * environment.subsurfaceIrradiance;
    const float3 color = direct + ambient.Sum() + ambient.emission;
    uint shadowDebug;
    LXSceneLight shadowLight;
    float3 shadowToLight;
    if (LXSceneShadowDebugLight(shadowDebug, shadowLight, shadowToLight))
    {
        const SpecialForwardLight selected = EvaluateSpecialLight(surface, shadowToLight,
            shadowLight.color.rgb * shadowLight.color.a);
        return float4(LXSceneShadowDebugColor(shadowDebug, position, surface.layers.material.normal,
                          shadowToLight, color, selected.local + selected.transmission),
                      LXSceneBlended(record) == 1u ? material.alpha : 1);
    }
    return float4(LXSceneBlended(record) != 0u ? LXForwardVolume(pixel, position, color) : color,
                  LXSceneBlended(record) == 1u ? material.alpha : 1);
}
#endif

float4 LXSceneColorPSEvaluate(LXSceneVertex input, bool frontFace)
{
#if defined(CE_EDITOR_MINIMAL_PREVIEW)
    input = LXScenePixelCenter(input);
    return float4(LXSceneUndecaledMaterial(input, frontFace).emission, 1);
#else
    input=LXScenePixelCenter(input);
    PrincipledSurface material = LXSceneMaterial(input, frontFace);
    const uint index = uint(input.clip.y) * lx_width + uint(input.clip.x);
    const PrincipledIblBakeSample sample = LXSceneReadIbl(input, frontFace, index);
    if (LXSceneBlended(input.record) == 1u && material.alpha <= 0.0) discard;
    // Fine derivatives must be evaluated before the per-pixel owner branch.
    if (input.reconstruction == 0u && LXSceneBlended(input.record) == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != LXSceneOwner(input.record))
        discard;
    const float3 view = PrincipledUnitNormal(lx_eye.xyz - input.position, material.normal);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) == 0
    // SSAO is half resolution. Sample with full-view UVs, as Deferred does.
    if (LXSceneBlended(input.record) == 0u) material.occlusion *= saturate(lx_ao.SampleLevel(gEnvironmentSampler,
        input.clip.xy / float2(lx_width, lx_height), 0).r);
#endif
    // Scene SSAO describes the opaque background before refraction. A
    // transmitting surface uses its authored AO instead of that other surface.
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0
    return LXSceneSpecialColor(material, input.clip.xy, input.position, index, sample, input.record);
#else
    if (sample.baseAverage.w < 0)
        return float4(1, 0, 1, 1);
    PrincipledLayeredIntegral integral;
    integral.base = ReadPrincipledIblIntegral(sample.baseSingleAlbedo, sample.baseAverage);
#if (LX_MATERIAL_FEATURE_MASK & 0x07C0u) != 0
    integral.coat = ReadPrincipledIblIntegral(sample.coatSingleAlbedo, sample.coatAverage);
    const PrincipledLayeredState layered = BuildLayeredState(material, view, integral);
#else
    integral.coat = PrincipledEmptyIntegral();
#endif
    float3 direct = float3(0);
    uint tile = 0u;
    bool tiled = false;
    const uint count = LXSceneLightCount(input.clip.xy, tile, tiled);
    for (uint i = 0; i < count; ++i)
    {
        const uint lightIndex = tiled ? lx_forwardIndices[tile * 32u + i] : i;
        const LXSceneLight light = LXSceneReadLight(lightIndex);
        const uint type = uint(light.position.w);
        float3 toLight;
        float attenuation = 1;
        if (type == 0u)
        {
            toLight = -normalize(light.direction.xyz);
            attenuation = (lightIndex + 1u == uint(max(lx_shadowSplits.w, 1.0))) ? LXSceneShadow(input.position, material.normal, toLight) : 1.0;
        }
        else
        {
            const float3 offset = light.position.xyz - input.position;
            const float distance = length(offset);
            toLight = offset / max(distance, 1e-4);
            attenuation = PbrLocalLightFalloff(light.attenuation, distance);
            if (type == 2u)
                attenuation *= PbrSpotFalloff(toLight, light.direction);
        }
#if (LX_MATERIAL_FEATURE_MASK & 0x07C0u) != 0
        direct += EvaluateLayeredLight(layered, toLight, light.color.rgb * light.color.a * attenuation);
#else
        const PbrSurface surface = BuildPbrSurface(input.position, view, material, float2(1, 0));
        if (dot(material.normal, view) > 0.0)
            direct += surface.EvaluateLight(toLight, light.color.rgb * light.color.a * attenuation);
#endif
    }
    float3 ambient = float3(0);
    if (lx_hasEnvironment != 0u)
    {
        ambient = EvaluateBakedPrincipledIbl(material, view, (LX_MATERIAL_FEATURE_MASK & 0x07C0u) != 0, sample);
    }
#if (LX_MATERIAL_FEATURE_MASK & 0x07C0u) != 0
    const float3 emission = layered.lowerTransmission * material.emission;
#else
    const float3 emission = material.emission;
#endif
    const float3 color = direct + ambient + emission;
    uint shadowDebug;
    LXSceneLight shadowLight;
    float3 shadowToLight;
    if (LXSceneShadowDebugLight(shadowDebug, shadowLight, shadowToLight))
    {
        const float3 radiance = shadowLight.color.rgb * shadowLight.color.a;
#if (LX_MATERIAL_FEATURE_MASK & 0x07C0u) != 0
        const float3 selected = EvaluateLayeredLight(layered, shadowToLight, radiance);
#else
        const float3 selected = dot(material.normal, view) > 0.0
            ? BuildPbrSurface(input.position, view, material, float2(1, 0)).EvaluateLight(shadowToLight, radiance)
            : float3(0);
#endif
        return float4(LXSceneShadowDebugColor(shadowDebug, input.position, material.normal, shadowToLight,
                          color, selected), LXSceneBlended(input.record) == 1u ? material.alpha : 1);
    }
    return float4(LXSceneBlended(input.record) != 0u ? LXForwardVolume(input.clip.xy, input.position, color) : color,
                  LXSceneBlended(input.record) == 1u ? material.alpha : 1);
#endif
 #endif
}
[shader("fragment")]
float4 LXSceneColorPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    // One material/lighting evaluation site for both raster replay and reconstruction.
    // Duplicating this large body makes the downstream optimizer process it twice.
    // Reconstruction still evaluates every unique quad identity with helper lanes.
    const bool reconstruct = input.reconstruction != 0u;
    uint2 own = uint2(0);
    uint uniqueLanes = 1u;
    if (reconstruct) {
        own = LXSceneLoadVisibility(uint2(input.clip.xy), input.record);
        uniqueLanes = LXSceneUniqueQuadIdentityLanes(own);
    }
    float4 output = (float4)0;
    [loop] for (uint lane = 0u; lane < (reconstruct ? 4u : 1u); ++lane) {
        const uint2 identity = QuadReadLaneAt(own, lane);
        if (!reconstruct || ((uniqueLanes & (1u << lane)) != 0u && identity.x == LXSceneIdentity(input.record).x))
        {
            bool face = frontFace;
            LXSceneVertex restored = input;
            if (reconstruct) restored = LXSceneReconstruct(input.clip, identity, face);
            const float4 value = LXSceneColorPSEvaluate(restored, face);
            if (!reconstruct || all(own == identity)) output = value;
        }
    }
    if (reconstruct && own.x != LXSceneIdentity(input.record).x) discard;
    return output;
}
