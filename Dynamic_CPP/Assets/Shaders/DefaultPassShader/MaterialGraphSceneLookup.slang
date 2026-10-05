#define LX_MATERIAL_FEATURE_MASK 0x07FFu
#ifdef __spirv__
// Avoid expanding dynamically indexed sensitivity constants into the kernel.
#define LX_FILM_SENSITIVITY_GPU_TABLE 1
#endif
#include "Includes/PrincipledIblLookup.slang"

cbuffer SceneLookupConstants : register(b0)
{
    uint lx_width, lx_height, lx_count, lx_reuse;
    uint lx_environment, lx_first, lx_dispatchCount, lx_precomputed;
    uint lx_importance, lx_source, lx_standalone, lx_approximate;
    uint lx_runtimeEvaluation, lx_reserved0, lx_reserved1, lx_reserved2;
};

// lx_approximate 0 integrates every changed pixel at the reference counts
// (gates and offline captures). Otherwise a changed pixel takes the split-sum
// approximation: analytic DFG with the prefiltered/irradiance cubemaps. It is
// final; the live view does not converge to the reference integration.
// baseAverage.w: -1 empty, 0 reference, 1 approximate. Consumers test < 0.
Texture2D<float4> lx_inputs_0 : register(t0);
Texture2D<float4> lx_inputs_1 : register(t1);
Texture2D<float4> lx_inputs_2 : register(t2);
Texture2D<float4> lx_inputs_3 : register(t3);
Texture2D<float4> lx_inputs_4 : register(t4);
Texture2D<float4> lx_inputs_5 : register(t5);
Texture2D<float4> lx_inputs_6 : register(t6);
Texture2D<float4> lx_inputs_7 : register(t7);
Texture2D<float4> lx_inputs_8 : register(t8);
Texture2D<float4> lx_inputs_9 : register(t9);
Texture2D<float4> lx_inputs_10 : register(t10);

float4 LXLoadSceneInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_inputs_0.Load(pixel);
        case 1u:
            return lx_inputs_1.Load(pixel);
        case 2u:
            return lx_inputs_2.Load(pixel);
        case 3u:
            return lx_inputs_3.Load(pixel);
        case 4u:
            return lx_inputs_4.Load(pixel);
        case 5u:
            return lx_inputs_5.Load(pixel);
        case 6u:
            return lx_inputs_6.Load(pixel);
        case 7u:
            return lx_inputs_7.Load(pixel);
        case 8u:
            return lx_inputs_8.Load(pixel);
        case 9u:
            return lx_inputs_9.Load(pixel);
        case 10u:
            return lx_inputs_10.Load(pixel);
        default:
            return float4(0);
    }
}
Texture2D<float4> lx_previousInputs_0 : register(t11);
Texture2D<float4> lx_previousInputs_1 : register(t12);
Texture2D<float4> lx_previousInputs_2 : register(t13);
Texture2D<float4> lx_previousInputs_3 : register(t14);
Texture2D<float4> lx_previousInputs_4 : register(t15);
Texture2D<float4> lx_previousInputs_5 : register(t16);
Texture2D<float4> lx_previousInputs_6 : register(t17);
Texture2D<float4> lx_previousInputs_7 : register(t18);
Texture2D<float4> lx_previousInputs_8 : register(t19);
Texture2D<float4> lx_previousInputs_9 : register(t20);
Texture2D<float4> lx_previousInputs_10 : register(t21);

float4 LXLoadPreviousSceneInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_previousInputs_0.Load(pixel);
        case 1u:
            return lx_previousInputs_1.Load(pixel);
        case 2u:
            return lx_previousInputs_2.Load(pixel);
        case 3u:
            return lx_previousInputs_3.Load(pixel);
        case 4u:
            return lx_previousInputs_4.Load(pixel);
        case 5u:
            return lx_previousInputs_5.Load(pixel);
        case 6u:
            return lx_previousInputs_6.Load(pixel);
        case 7u:
            return lx_previousInputs_7.Load(pixel);
        case 8u:
            return lx_previousInputs_8.Load(pixel);
        case 9u:
            return lx_previousInputs_9.Load(pixel);
        case 10u:
            return lx_previousInputs_10.Load(pixel);
        default:
            return float4(0);
    }
}
Texture2D<uint> lx_owners : register(t22);
TextureCube<float4> gEnvironment : register(t23);
Texture2D<float4> gEnvironmentSource : register(t30);
StructuredBuffer<PrincipledIblBakeSample> lx_previous : register(t24);
RWStructuredBuffer<PrincipledIblBakeSample> lx_samples : register(u0);
RWStructuredBuffer<uint> lx_statistics : register(u1);
SamplerState gEnvironmentSampler : register(s0);
TextureCube<float4> gIrradiance : register(t25);
TextureCube<float4> gPrefiltered : register(t26);
SamplerState gIblSampler : register(s1);
#define LX_SCENE_LOOKUP_PRECOMPUTED 1
#include "Includes/PrincipledEnvironmentBake.slang"

[shader("compute")]
[numthreads(1, 1, 1)]
void LXSceneLookupClear(uint3 id : SV_DispatchThreadID)
{
    for (uint i = 0; i < 8; ++i)
    {
        lx_statistics[i] = 0;
    }
    lx_statistics[5] = lx_runtimeEvaluation;
}

// Statistics: 0 reference integrations, 1 reused, 2 visible, 3 rejected,
// 4 split-sum approximations.

#define LX_SPLIT_SUM_PRECOMPUTED lx_precomputed
#define LX_SPLIT_SUM_ENVIRONMENT lx_environment
#define LX_SPLIT_SUM_IRRADIANCE gIrradiance
#define LX_SPLIT_SUM_PREFILTERED gPrefiltered
#define LX_SPLIT_SUM_SAMPLER gIblSampler
#define LX_SPLIT_SUM_RADIANCE BakeRadiance
#include "Includes/PrincipledRuntimeIbl.slang"

PrincipledIblBakePoint LXReadScenePoint(int2 pixel)
{
    PrincipledIblBakePoint point;
    point.baseAlpha = lx_inputs_0.Load(int3(pixel, 0));
    point.normalRoughness = lx_inputs_1.Load(int3(pixel, 0));
    point.metalIorLevelAo = lx_inputs_2.Load(int3(pixel, 0));
    point.tintAnisotropy = lx_inputs_3.Load(int3(pixel, 0));
    point.emissionColorStrength = lx_inputs_4.Load(int3(pixel, 0));
    point.coatWeightRoughIorFilmThickness = lx_inputs_5.Load(int3(pixel, 0));
    point.coatTintFilmIor = lx_inputs_6.Load(int3(pixel, 0));
    point.coatNormalSheenWeight = lx_inputs_7.Load(int3(pixel, 0));
    point.sheenTintRoughness = lx_inputs_8.Load(int3(pixel, 0));
    point.tangentRotation = lx_inputs_9.Load(int3(pixel, 0));
    point.viewTier = lx_inputs_10.Load(int3(pixel, 0));
    return point;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneLookupBake(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_dispatchCount)
        return;
    const uint index = lx_first + id.x;
    const int2 pixel = int2(index % lx_width, index / lx_width);
    PrincipledIblBakeSample sample = (PrincipledIblBakeSample)0;
    sample.baseAverage.w = -1;
    if (lx_standalone != 0u ? lx_inputs_0.Load(int3(pixel, 0)).w <= 0.0
                            : (lx_owners.Load(int3(pixel, 0)) & 0x80000000u) == 0u)
    {
        // Only this validity component is consumed for an uncovered pixel.
        // Avoid writing 144 bytes per background pixel on a full viewport.
        lx_samples[index].baseAverage.w = -1;
        return;
    }
    InterlockedAdd(lx_statistics[2], 1);
    bool same = lx_reuse != 0u;
    if (same)
    {
        for (uint field = 0; field < 11; ++field)
            same = same && all(asuint(LXLoadSceneInput(field, int3(pixel, 0)))
                               == asuint(LXLoadPreviousSceneInput(field, int3(pixel, 0))));
        same = same && lx_previous[index].baseAverage.w >= 0;
    }
    if (same)
    {
        lx_samples[index] = lx_previous[index];
        InterlockedAdd(lx_statistics[1], 1);
        return;
    }
    bool finite = true;
    for (uint field = 0; field < 11; ++field)
        finite = finite && PrincipledValidBakeVector(LXLoadSceneInput(field, int3(pixel, 0)));
    const PrincipledIblBakePoint point = LXReadScenePoint(pixel);
    if (!finite || (point.viewTier.w != 0.0 && point.viewTier.w != 1.0)
        || dot(point.viewTier.xyz, point.viewTier.xyz) <= 1e-20)
    {
        lx_samples[index] = sample;
        InterlockedAdd(lx_statistics[3], 1);
        return;
    }
    const PrincipledSurface material = EvaluateIblBakePoint(point);
    const float3 view = normalize(point.viewTier.xyz);
    const bool layered = point.viewTier.w == 1.0;
    if (lx_approximate != 0u)
    {
        lx_samples[index] = LXSplitSumSample(material, view, layered);
        InterlockedAdd(lx_statistics[4], 1);
        return;
    }
    PrincipledLayeredIntegral integral;
    integral.base = layered ? IntegrateLayeredReflection(material, view, false)
                            : IntegratePrincipledIbl(material, saturate(dot(material.normal, view)));
    integral.coat = layered && material.coatWeight > 0.0 ? IntegrateLayeredReflection(material, view, true)
                                                       : PrincipledEmptyIntegral();
    sample.baseSingleAlbedo = float4(integral.base.singleScatter, integral.base.directionalAlbedo);
    sample.baseAverage = float4(integral.base.averageFresnel, 0);
    sample.coatSingleAlbedo = float4(integral.coat.singleScatter, integral.coat.directionalAlbedo);
    sample.coatAverage = float4(integral.coat.averageFresnel, 0);
    if (lx_environment != 0u)
    {
        sample.irradiance = float4(BakeDiffuse(material.normal), 0);
        // No radiance convolution is consumed when this lobe carries exactly
        // zero energy (e.g. emission-only materials with Specular Level 0).
        sample.basePrefiltered = float4(any(integral.base.singleScatter > 0.0)
            ? BakeReflection(material, view, layered, false) : float3(0), 0);
        sample.coatPrefiltered = float4(layered && material.coatWeight > 0.0 && any(integral.coat.singleScatter > 0.0)
            ? BakeReflection(material, view, true, true) : float3(0), 0);
        sample.coatIrradiance = float4(layered && material.coatWeight > 0.0
            ? BakeDiffuse(material.coatNormal) : float3(0), 0);
        sample.sheenIrradiance = float4(layered ? BakeSheen(BuildLayeredState(material, view, integral)) : float3(0), 0);
    }
    lx_samples[index] = sample;
    InterlockedAdd(lx_statistics[0], 1);
}
