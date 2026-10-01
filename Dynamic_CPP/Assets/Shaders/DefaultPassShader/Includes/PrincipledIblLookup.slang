#pragma once

#include "PrincipledLayered.slang"

// Version 1 evaluated-point ABI, shared with MaterialGraphIblBake.h.
// Texture-varying inputs must be evaluated before generating a request. A
// sample is valid only for that material, world frame, view and environment.
struct PrincipledIblBakePoint
{
    float4 baseAlpha;
    float4 normalRoughness;
    float4 metalIorLevelAo;
    float4 tintAnisotropy;
    float4 emissionColorStrength;
    float4 coatWeightRoughIorFilmThickness;
    float4 coatTintFilmIor;
    float4 coatNormalSheenWeight;
    float4 sheenTintRoughness;
    float4 tangentRotation;
    float4 viewTier;
};

struct PrincipledIblBakeSample
{
    float4 baseSingleAlbedo;
    float4 baseAverage;
    float4 coatSingleAlbedo;
    float4 coatAverage;
    float4 irradiance;
    float4 basePrefiltered;
    float4 coatPrefiltered;
    float4 sheenIrradiance;
    float4 coatIrradiance;
};

bool PrincipledValidBakeVector(float4 value)
{
    return all(isfinite(value)) && all(abs(value) <= 1e6);
}

// Same domain as IsValidIblBakePoint on CPU. GPU-authored points need this
// check before normalization/integration, including the explicit -1 marker.
// Rasterized Scene surfaces can have a grazing/back-facing shading normal.
// That is a valid point with an empty reflection integral, not missing residency.
bool IsValidScenePrincipledIblBakePoint(PrincipledIblBakePoint point)
{
    if (!PrincipledValidBakeVector(point.baseAlpha) || !PrincipledValidBakeVector(point.normalRoughness)
        || !PrincipledValidBakeVector(point.metalIorLevelAo) || !PrincipledValidBakeVector(point.tintAnisotropy)
        || !PrincipledValidBakeVector(point.emissionColorStrength)
        || !PrincipledValidBakeVector(point.coatWeightRoughIorFilmThickness)
        || !PrincipledValidBakeVector(point.coatTintFilmIor)
        || !PrincipledValidBakeVector(point.coatNormalSheenWeight)
        || !PrincipledValidBakeVector(point.sheenTintRoughness)
        || !PrincipledValidBakeVector(point.tangentRotation) || !PrincipledValidBakeVector(point.viewTier)
        || (point.viewTier.w != 0.0 && point.viewTier.w != 1.0))
    {
        return false;
    }
    const float viewLength = length(point.viewTier.xyz);
    if (viewLength <= 1e-10)
    {
        return false;
    }
    return true;
}

// Standalone directional bake requests retain their front-hemisphere contract.
bool IsValidPrincipledIblBakePoint(PrincipledIblBakePoint point)
{
    if (!IsValidScenePrincipledIblBakePoint(point))
    {
        return false;
    }
    const float viewLength = length(point.viewTier.xyz);
    const float3 normal = length(point.normalRoughness.xyz) <= 1e-10
        ? float3(0, 0, 1) : normalize(point.normalRoughness.xyz);
    return dot(normal, point.viewTier.xyz / viewLength) >= 1e-4
        && (point.viewTier.w == 1.0 || (point.tintAnisotropy.w == 0.0
            && point.coatWeightRoughIorFilmThickness.x == 0.0
            && point.coatWeightRoughIorFilmThickness.w == 0.0 && point.coatNormalSheenWeight.w == 0.0));
}

PrincipledSurface EvaluateIblBakePoint(PrincipledIblBakePoint point)
{
    MaterialInputs inputs = DefaultMaterialInputs();
    inputs.baseColor = point.baseAlpha.rgb;
    inputs.alpha = point.baseAlpha.a;
    inputs.normal = point.normalRoughness.xyz;
    inputs.roughness = point.normalRoughness.w;
    inputs.metallic = point.metalIorLevelAo.x;
    inputs.ior = point.metalIorLevelAo.y;
    inputs.specularIorLevel = point.metalIorLevelAo.z;
    inputs.occlusion = point.metalIorLevelAo.w;
    inputs.specularTint = point.tintAnisotropy.rgb;
    inputs.anisotropy = point.tintAnisotropy.w;
    inputs.emissionColor = point.emissionColorStrength.rgb;
    inputs.emissionStrength = point.emissionColorStrength.w;
    inputs.coatWeight = point.coatWeightRoughIorFilmThickness.x;
    inputs.coatRoughness = point.coatWeightRoughIorFilmThickness.y;
    inputs.coatIor = point.coatWeightRoughIorFilmThickness.z;
    inputs.thinFilmThickness = point.coatWeightRoughIorFilmThickness.w;
    inputs.coatTint = point.coatTintFilmIor.rgb;
    inputs.thinFilmIor = point.coatTintFilmIor.w;
    inputs.coatNormal = point.coatNormalSheenWeight.xyz;
    inputs.sheenWeight = point.coatNormalSheenWeight.w;
    inputs.sheenTint = point.sheenTintRoughness.rgb;
    inputs.sheenRoughness = point.sheenTintRoughness.w;
    inputs.tangent = point.tangentRotation.xyz;
    inputs.anisotropyRotation = point.tangentRotation.w;
    return EvaluateMaterial(inputs);
}

PrincipledIblIntegral ReadPrincipledIblIntegral(float4 singleAlbedo, float4 average)
{
    PrincipledIblIntegral result;
    result.singleScatter = singleAlbedo.rgb;
    result.directionalAlbedo = singleAlbedo.a;
    result.averageFresnel = average.rgb;
    return result;
}

// Production consumer: fixed work, no hemisphere integration or sampling loop.
float3 EvaluateBakedPrincipledIbl(PrincipledSurface material, float3 view,
    bool layered, PrincipledIblBakeSample sample)
{
    if (sample.baseAverage.w < 0.0)
    {
        return float3(0);
    }
    PrincipledLayeredIntegral integral;
    integral.base = ReadPrincipledIblIntegral(sample.baseSingleAlbedo, sample.baseAverage);
    if (!layered)
    {
        return EvaluatePrincipledIbl(material, saturate(dot(material.normal, view)),
            sample.irradiance.rgb, sample.basePrefiltered.rgb, integral.base).Sum();
    }
    integral.coat = ReadPrincipledIblIntegral(sample.coatSingleAlbedo, sample.coatAverage);
    const PrincipledLayeredState state = BuildLayeredState(material, view, integral);
    return EvaluateLayeredIbl(state, sample.irradiance.rgb, sample.basePrefiltered.rgb,
        sample.coatPrefiltered.rgb, sample.sheenIrradiance.rgb, sample.coatIrradiance.rgb).Sum();
}
