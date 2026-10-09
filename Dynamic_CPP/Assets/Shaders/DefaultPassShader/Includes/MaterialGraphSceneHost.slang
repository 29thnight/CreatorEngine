#pragma once
#define LX_SCENE_VOLUME_TRANSPORT 1
#include "MaterialGraphSurfacePoint.slang"
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
};

StructuredBuffer<LXSurfacePoint> lx_vertices : register(t0);
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
};

LXSceneVertex LXSceneVertexAt(uint index, uint owner, float4x4 viewProjection)
{
    const LXSurfacePoint source = lx_vertices[index];
    LXSceneVertex output;
    output.clip = mul(float4(source.position.xyz, 1), viewProjection);
    output.position = source.position.xyz;
    output.normal = source.normal.xyz;
    output.tangent = source.tangent.xyz;
    output.bitangent = source.bitangent.xyz;
    output.uv = source.uvLod.xy;
    output.owner = owner;
    return output;
}

[shader("vertex")]
LXSceneVertex LXSceneVS(uint index : SV_VertexID, uint instanceId : SV_InstanceID)
{
    return LXSceneVertexAt(index, lx_visibleOwners[instanceId], lx_viewProjection);
}

// Chunk-local topology over the same transformed vertices used by LXSceneVS.
// Scene preparation pins this immutable stream through submission completion.
StructuredBuffer<uint> lx_meshlets : register(t155);

groupshared uint lx_meshletOutside;

// Intersect the outside-plane masks of the actual posed vertices. A common
// plane proves the entire convex hull is outside; crossings/unknowns survive.
bool LXSceneMeshletVisible(uint remap, uint count, uint lane, float4x4 projection)
{
    if (lane == 0u)
    {
        lx_meshletOutside = 63u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane < count)
    {
        const float4 clip = mul(float4(lx_vertices[lx_meshlets[remap + lane]].position.xyz, 1), projection);
        const float epsilon = max(1.0, max(abs(clip.w), max(abs(clip.x), max(abs(clip.y), abs(clip.z))))) * 0.00001;
        uint mask = 0u;
        if (all(isfinite(clip)))
        {
            mask = (clip.x < -clip.w - epsilon ? 1u : 0u) |
                (clip.x > clip.w + epsilon ? 2u : 0u) |
                (clip.y < -clip.w - epsilon ? 4u : 0u) |
                (clip.y > clip.w + epsilon ? 8u : 0u) |
                (clip.z < -epsilon ? 16u : 0u) |
                (clip.z > clip.w + epsilon ? 32u : 0u);
        }
        InterlockedAnd(lx_meshletOutside, mask);
    }
    GroupMemoryBarrierWithGroupSync();
    return lx_meshletOutside == 0u;
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void LXSceneMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    out vertices LXSceneVertex vertices[64], out indices uint3 triangles[126])
{
    const uint descriptor = lx_meshlets[1] + group.x * 4u;
    const uint remap = lx_meshlets[2] + lx_meshlets[descriptor];
    const uint indices = lx_meshlets[3] + lx_meshlets[descriptor + 1u];
    const uint vertexCount = lx_meshlets[descriptor + 2u];
    const uint triangleCount = lx_meshlets[descriptor + 3u];
    const bool visible = LXSceneMeshletVisible(remap, vertexCount, lane, lx_viewProjection);
    SetMeshOutputCounts(visible ? vertexCount : 0u, visible ? triangleCount : 0u);
    if (!visible)
    {
        return;
    }
    if (lane < vertexCount)
    {
        LXSceneVertex vertex = LXSceneVertexAt(lx_meshlets[remap + lane], lx_drawOwner, lx_viewProjection);
        vertices[lane] = vertex;
    }
    for (uint triangle = lane; triangle < triangleCount; triangle += 64u)
    {
        const uint index = indices + triangle * 3u;
        triangles[triangle] = uint3(lx_meshlets[index], lx_meshlets[index + 1u], lx_meshlets[index + 2u]);
    }
}

// Hardware coverage uses snapped subpixel vertices. Reconstruct attributes at
// the true pixel-center ray on the original triangle plane. Attribute and
// position derivatives span the same affine world-space barycentric plane;
// solving its homogeneous projection avoids extra index/vertex buffer reads.
// Coverage/depth remain owned by the rasterizer. Derivatives precede guards.
LXSceneVertex LXScenePixelCenter(LXSceneVertex input)
{
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
    uint2 lx_shadowPadding;
};

[shader("vertex")]
LXSceneVertex LXSceneShadowVS(uint index : SV_VertexID)
{
    return LXSceneVertexAt(index, 0u, lx_shadowViewProjection);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void LXSceneShadowMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    out vertices LXSceneVertex vertices[64], out indices uint3 triangles[126])
{
    const uint descriptor = lx_meshlets[1] + group.x * 4u;
    const uint remap = lx_meshlets[2] + lx_meshlets[descriptor];
    const uint indices = lx_meshlets[3] + lx_meshlets[descriptor + 1u];
    const uint vertexCount = lx_meshlets[descriptor + 2u];
    const uint triangleCount = lx_meshlets[descriptor + 3u];
    const bool visible = LXSceneMeshletVisible(remap, vertexCount, lane, lx_shadowViewProjection);
    SetMeshOutputCounts(visible ? vertexCount : 0u, visible ? triangleCount : 0u);
    if (!visible)
    {
        return;
    }
    if (lane < vertexCount)
    {
        vertices[lane] = LXSceneVertexAt(lx_meshlets[remap + lane], 0u, lx_shadowViewProjection);
    }
    for (uint triangle = lane; triangle < triangleCount; triangle += 64u)
    {
        const uint index = indices + triangle * 3u;
        triangles[triangle] = uint3(lx_meshlets[index], lx_meshlets[index + 1u], lx_meshlets[index + 2u]);
    }
}

[shader("fragment")]
void LXSceneShadowPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    ApplyPbrCoverage(saturate(generated.surfaceInputs.alpha), lx_shadowCutoff, lx_shadowCoverage, frontFace);
}

[shader("fragment")]
void LXSceneDepthPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    input = LXScenePixelCenter(input);
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    ApplyPbrCoverage(saturate(generated.surfaceInputs.alpha), lx_cutoff, lx_coverage, frontFace);
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
        if ((lx_coverage & PBR_COVERAGE_DOUBLE_SIDED) != 0u && !frontFace)
            raw.normal = -raw.normal;
    }
    return raw;
}

PrincipledIblBakePoint LXScenePoint(LXSceneVertex input, bool frontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    const MaterialInputs raw = LXSceneDecalInputs(generated.surfaceInputs, input, frontFace);
    const float3 normal = PrincipledUnitNormal(raw.normal, context.normal);
    // Preserve authored IOR/level and the unrotated tangent. The compute baker
    // applies material evaluation once, exactly as the evaluated-point path.
    PrincipledIblBakePoint point;
    point.baseAlpha = float4(raw.baseColor, ApplyPbrCoverage(saturate(raw.alpha), lx_cutoff, lx_coverage, frontFace));
    point.normalRoughness = float4(OrientPbrNormal(normal, lx_coverage, frontFace), raw.roughness);
    point.metalIorLevelAo = float4(raw.metallic, raw.ior, raw.specularIorLevel, raw.occlusion);
    point.tintAnisotropy = float4(raw.specularTint, raw.anisotropy);
    point.emissionColorStrength = float4(raw.emissionColor, raw.emissionStrength);
    point.coatWeightRoughIorFilmThickness = float4(raw.coatWeight, raw.coatRoughness, raw.coatIor, raw.thinFilmThickness);
    point.coatTintFilmIor = float4(raw.coatTint, raw.thinFilmIor);
    point.coatNormalSheenWeight = float4(OrientPbrNormal(PrincipledUnitNormal(raw.coatNormal, context.normal),
                                                      lx_coverage, frontFace), raw.sheenWeight);
    point.sheenTintRoughness = float4(raw.sheenTint, raw.sheenRoughness);
    const float3 tangent = PrincipledUnitNormal(raw.tangent - normal * dot(raw.tangent, normal),
                                               PrincipledProjectedTangent(context.tangent, normal));
    const bool flipped = (lx_coverage & PBR_COVERAGE_DOUBLE_SIDED) != 0u && !frontFace;
    point.tangentRotation = float4(tangent, flipped ? -raw.anisotropyRotation : raw.anisotropyRotation);
    point.viewTier = float4(PrincipledUnitNormal(lx_eye.xyz - input.position, normal),
                            (LX_MATERIAL_FEATURE_MASK & 0x17C0u) != 0 ? 1 : 0);
    // Principled core graphs use the same GGX closures with film/coat disabled.
    // This lookup tag describes the evaluation model, not the authoring tier.
    // Fine UV derivatives and graph evaluation precede this divergent owner test.
    if (lx_blended == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != lx_drawOwner)
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

[shader("fragment")]
LXSceneLookup0 LXSceneLookup0PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
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
LXSceneLookup1 LXSceneLookup1PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledIblBakePoint point = LXScenePoint(input, frontFace);
    LXSceneLookup1 output;
    output.fields[0] = point.sheenTintRoughness;
    output.fields[1] = point.tangentRotation;
    output.fields[2] = point.viewTier;
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

PrincipledSurface LXSceneEvaluateMaterial(LXGeneratedMaterial generated, LXMaterialContext context, bool frontFace)
{
    PrincipledSurface surface = LXEvaluateGeneratedSurface(generated, context);
    surface.alpha = ApplyPbrCoverage(surface.alpha, lx_cutoff, lx_coverage, frontFace);
    surface.normal = OrientPbrNormal(surface.normal, lx_coverage, frontFace);
    surface.coatNormal = OrientPbrNormal(surface.coatNormal, lx_coverage, frontFace);
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
    const LXMaterialContext context = LXSceneContext(input);
    const LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    return LXSceneEvaluateMaterial(generated, context, frontFace);
}

PrincipledSurface LXSceneMaterial(LXSceneVertex input, bool frontFace)
{
    const LXMaterialContext context = LXSceneContext(input);
    LXGeneratedMaterial generated = LXGenerateMaterial(context, LXBoundMaterialParameters());
    generated.surfaceInputs = LXSceneDecalInputs(generated.surfaceInputs, input, frontFace);
    return LXSceneEvaluateMaterial(generated, context, frontFace);
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
    if (lx_blended == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != lx_drawOwner)
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

[shader("fragment")]
LXSceneGBuffer LXSceneGBufferPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    input=LXScenePixelCenter(input);
    const PrincipledSurface surface = LXSceneUndecaledMaterial(input, frontFace);
    if (lx_blended == 1u && surface.alpha <= 0.0) discard;
    LXSceneGBuffer output;
    output.diffuse = float4(surface.baseColor, surface.alpha);
    output.metalRough = float4(surface.occlusion, surface.roughness, surface.metallic, 1);
    output.normal = float4(surface.normal * 0.5 + 0.5, 1);
    output.emissive = float4(surface.emission, 1);
    output.owner = input.owner;
    return output;
}

// Paired transformed streams use the same current topology. Previous skin and
// object transforms are evaluated by the existing surface-geometry compute stage.
StructuredBuffer<LXSurfacePoint> lx_previousVertices : register(t156);
struct LXTemporalVertex
{
    LXSceneVertex surface;
    float4 currentClip : TEXCOORD6;
    float4 previousClip : TEXCOORD7;
};
[shader("vertex")]
LXTemporalVertex LXSceneTemporalVS(uint index : SV_VertexID, uint instanceId : SV_InstanceID)
{
    LXTemporalVertex output;
    output.surface = LXSceneVertexAt(index, lx_visibleOwners[instanceId], lx_viewProjection);
    output.currentClip = mul(float4(lx_vertices[index].position.xyz, 1), lx_temporalCurrentProjection);
    output.previousClip = mul(float4(lx_previousVertices[index].position.xyz, 1), lx_temporalPreviousProjection);
    return output;
}
struct LXTemporalOutput
{
    float2 motion : SV_Target0;
    float reactive : SV_Target1;
    float transparency : SV_Target2;
    float responsive : SV_Target3;
};
[shader("fragment")]
LXTemporalOutput LXSceneTemporalPS(LXTemporalVertex input, bool frontFace : SV_IsFrontFace)
{
    // Match the material host's true-pixel-center correction in BOTH time samples.
    // Derivatives precede material coverage/discard, including alpha/SDF edges.
    const float3 dx=ddx_fine(input.surface.position), dy=ddy_fine(input.surface.position);
    const float4 previousDx=ddx_fine(input.previousClip), previousDy=ddy_fine(input.previousClip);
    const LXSceneVertex centered=LXScenePixelCenter(input.surface);
    const float3 delta=centered.position-input.surface.position;
    const float xx=dot(dx,dx), xy=dot(dx,dy), yy=dot(dy,dy), determinant=xx*yy-xy*xy;
    if (isfinite(determinant) && abs(determinant)>1e-20)
    {
        const float2 step=float2(dot(delta,dx)*yy-dot(delta,dy)*xy,
            dot(delta,dy)*xx-dot(delta,dx)*xy)/determinant;
        if(all(isfinite(step)))input.previousClip+=previousDx*step.x+previousDy*step.y;
    }
    input.currentClip=mul(float4(centered.position,1),lx_temporalCurrentProjection);
    const PrincipledSurface surface = LXSceneUndecaledMaterial(centered, frontFace);
    if (lx_blended == 1u && surface.alpha <= 0.01) discard;
    LXTemporalOutput output;
    const bool valid = lx_temporalHistoryValid != 0u && input.currentClip.w > 0.000001 &&
        input.previousClip.w > 0.000001 && all(isfinite(input.previousClip));
    output.motion = valid ? (input.previousClip.xy / input.previousClip.w -
        input.currentClip.xy / input.currentClip.w) * float2(0.5 * lx_width, -0.5 * lx_height) : float2(0);
    output.transparency = lx_blended != 0u ? saturate(surface.alpha) : 0;
    output.reactive = lx_blended != 0u ? 1 : 0;
    output.responsive = valid ? 0 : 1;
    return output;
}

// Deferred·Forward 와 같은 식(CascadedShadow.slang)을 쓴다.
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

[shader("fragment")]
LXSceneSubsurfaceCapture LXSceneSubsurfacePS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    input=LXScenePixelCenter(input);
    // Evaluate quad derivatives before alpha/face coverage can discard a lane.
    const float area = length(cross(ddx_fine(input.position), ddy_fine(input.position)));
    const PrincipledSurface material = LXSceneMaterial(input, frontFace);
    const uint index = uint(input.clip.y) * lx_width + uint(input.clip.x);
    const PrincipledIblBakeSample sample = LXSceneReadIbl(input, frontFace, index);
    if (lx_blended == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != lx_drawOwner)
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

float4 LXSceneSpecialColor(PrincipledSurface material, float2 pixel, float3 position, uint index,
    PrincipledIblBakeSample sample)
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
                      lx_blended == 1u ? material.alpha : 1);
    }
    return float4(lx_blended != 0u ? LXForwardVolume(pixel, position, color) : color,
                  lx_blended == 1u ? material.alpha : 1);
}
#endif

[shader("fragment")]
float4 LXSceneColorPS(LXSceneVertex input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    input=LXScenePixelCenter(input);
    PrincipledSurface material = LXSceneMaterial(input, frontFace);
    const uint index = uint(input.clip.y) * lx_width + uint(input.clip.x);
    const PrincipledIblBakeSample sample = LXSceneReadIbl(input, frontFace, index);
    if (lx_blended == 1u && material.alpha <= 0.0) discard;
    // Fine derivatives must be evaluated before the per-pixel owner branch.
    if (lx_blended == 0u && lx_owners.Load(int3(uint2(input.clip.xy), 0)) != lx_drawOwner)
        discard;
    const float3 view = PrincipledUnitNormal(lx_eye.xyz - input.position, material.normal);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) == 0
    // SSAO is half resolution. Sample with full-view UVs, as Deferred does.
    if (lx_blended == 0u) material.occlusion *= saturate(lx_ao.SampleLevel(gEnvironmentSampler,
        input.clip.xy / float2(lx_width, lx_height), 0).r);
#endif
    // Scene SSAO describes the opaque background before refraction. A
    // transmitting surface uses its authored AO instead of that other surface.
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0
    return LXSceneSpecialColor(material, input.clip.xy, input.position, index, sample);
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
                          color, selected), lx_blended == 1u ? material.alpha : 1);
    }
    return float4(lx_blended != 0u ? LXForwardVolume(input.clip.xy, input.position, color) : color,
                  lx_blended == 1u ? material.alpha : 1);
#endif
}
