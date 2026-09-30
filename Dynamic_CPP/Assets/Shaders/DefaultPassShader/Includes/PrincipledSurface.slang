#pragma once

// Logical material ABI. These structs are local shader values, not a cbuffer,
// GBuffer layout, or a host-side byte layout. Generated material code fills
// MaterialInputs; every render route consumes PrincipledSurface.
typedef uint MaterialFeatureMask;

static const MaterialFeatureMask kMaterialFeatureBaseColor = 1u << 0u;
static const MaterialFeatureMask kMaterialFeatureMetallicRoughness = 1u << 1u;
static const MaterialFeatureMask kMaterialFeatureNormal = 1u << 2u;
static const MaterialFeatureMask kMaterialFeatureAlpha = 1u << 3u;
static const MaterialFeatureMask kMaterialFeatureEmission = 1u << 4u;
static const MaterialFeatureMask kMaterialFeatureOcclusion = 1u << 5u;
static const MaterialFeatureMask kMaterialFeatureSpecularIor = 1u << 6u;
static const MaterialFeatureMask kMaterialFeatureCoat = 1u << 7u;
static const MaterialFeatureMask kMaterialFeatureSheen = 1u << 8u;
static const MaterialFeatureMask kMaterialFeatureAnisotropy = 1u << 9u;
static const MaterialFeatureMask kMaterialFeatureIridescence = 1u << 10u;
static const MaterialFeatureMask kMaterialFeatureTransmission = 1u << 11u;
static const MaterialFeatureMask kMaterialFeatureSubsurface = 1u << 12u;
static const MaterialFeatureMask kMaterialFeatureVolume = 1u << 13u;

static const MaterialFeatureMask kMaterialStandardFeatures =
    kMaterialFeatureBaseColor | kMaterialFeatureMetallicRoughness
    | kMaterialFeatureNormal | kMaterialFeatureAlpha
    | kMaterialFeatureEmission | kMaterialFeatureOcclusion
    | kMaterialFeatureSpecularIor;
static const MaterialFeatureMask kMaterialLayeredFeatures =
    kMaterialFeatureCoat | kMaterialFeatureSheen
    | kMaterialFeatureAnisotropy | kMaterialFeatureIridescence;
static const MaterialFeatureMask kMaterialSpecialFeatures =
    kMaterialFeatureTransmission | kMaterialFeatureSubsurface
    | kMaterialFeatureVolume;

// Cooked graph code will supply this define. The current fixed Standard path
// has the full legacy feature set; no per-pixel feature-mask branch is added.
#ifndef LX_MATERIAL_FEATURE_MASK
#define LX_MATERIAL_FEATURE_MASK 0x003Fu
#endif

// This is a numeric cook-time define. Reject unsupported authored features
// instead of rendering them with Standard lighting and losing their meaning.
#if (LX_MATERIAL_FEATURE_MASK & ~0x3FFFu) != 0
#error Material evaluation supports only known features (0x3FFF).
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0 && (LX_MATERIAL_FEATURE_MASK & 0x0040u) == 0
#error Layered features require core Specular/IOR evaluation.
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0 && (LX_MATERIAL_FEATURE_MASK & 0x0040u) == 0
#error Special surface features require core Specular/IOR evaluation.
#endif

static const MaterialFeatureMask kMaterialFeatureMask = LX_MATERIAL_FEATURE_MASK;

bool HasMaterialFeature(MaterialFeatureMask mask, MaterialFeatureMask feature)
{
    return (mask & feature) != 0u;
}

struct MaterialInputs
{
    float3 baseColor;
    float alpha;
    float3 normal;
    float roughness;
    float metallic;
    float3 emissionColor;
    float emissionStrength;
    float occlusion;

    float ior;
    float specularIorLevel;
    float3 specularTint;

    float coatWeight;
    float coatRoughness;
    float coatIor;
    float3 coatTint;
    float3 coatNormal;
    float sheenWeight;
    float sheenRoughness;
    float3 sheenTint;
    float anisotropy;
    float anisotropyRotation;
    float3 tangent;
    float thinFilmThickness;
    float thinFilmIor;

    float transmissionWeight;
    float subsurfaceWeight;
    float3 subsurfaceRadius;
    float subsurfaceScale;
    float subsurfaceIor;
    float subsurfaceAnisotropy;
};

struct PrincipledSurface
{
    float3 baseColor;
    float alpha;
    float3 normal;
    float roughness;
    float metallic;
    float3 emission;
    float occlusion;
    float ior;
    float specularIorLevel;
    float3 dielectricF0;
    float3 metalF0;
    float3 metalF82Correction;
    float coatWeight;
    float coatRoughness;
    float coatIor;
    float3 coatTint;
    float3 coatNormal;
    float sheenWeight;
    float sheenRoughness;
    float3 sheenTint;
    float anisotropy;
    float anisotropyRotation;
    float3 tangent;
    float thinFilmThickness;
    float thinFilmIor;
    float transmissionWeight;
    float transmissionIor;
    float3 transmissionF0;
    float transmissionFilmIor;
    float subsurfaceWeight;
    float3 subsurfaceRadius;
    float subsurfaceScale;
    float subsurfaceIor;
    float subsurfaceAnisotropy;
};

MaterialInputs DefaultMaterialInputs(float3 geometricNormal = float3(0.0, 0.0, 1.0),
    float3 geometricTangent = float3(1.0, 0.0, 0.0))
{
    MaterialInputs inputs;
    inputs.baseColor = float3(0.8);
    inputs.alpha = 1.0;
    inputs.normal = geometricNormal;
    inputs.roughness = 0.5;
    inputs.metallic = 0.0;
    inputs.emissionColor = float3(1.0);
    inputs.emissionStrength = 0.0;
    inputs.occlusion = 1.0;
    inputs.ior = 1.5;
    inputs.specularIorLevel = 0.5;
    inputs.specularTint = float3(1.0);
    inputs.coatWeight = 0.0;
    inputs.coatRoughness = 0.03;
    inputs.coatIor = 1.5;
    inputs.coatTint = float3(1.0);
    inputs.coatNormal = geometricNormal;
    inputs.sheenWeight = 0.0;
    inputs.sheenRoughness = 0.5;
    inputs.sheenTint = float3(1.0);
    inputs.anisotropy = 0.0;
    inputs.anisotropyRotation = 0.0;
    inputs.tangent = geometricTangent;
    inputs.thinFilmThickness = 0.0;
    inputs.thinFilmIor = 1.33;
    inputs.transmissionWeight = 0.0;
    inputs.subsurfaceWeight = 0.0;
    inputs.subsurfaceRadius = float3(1.0, 0.2, 0.1);
    inputs.subsurfaceScale = 0.05;
    inputs.subsurfaceIor = 1.4;
    inputs.subsurfaceAnisotropy = 0.0;
    return inputs;
}

float3 PrincipledUnitNormal(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return all(isfinite(value)) && isfinite(lengthSquared) && lengthSquared > 1e-20
        ? value * rsqrt(lengthSquared) : fallback;
}

PrincipledSurface EvaluateMaterial(MaterialInputs inputs,
    float3 geometricNormal = float3(0.0, 0.0, 1.0),
    float3 geometricTangent = float3(1.0, 0.0, 0.0))
{
    PrincipledSurface surface;
    surface.baseColor = max(inputs.baseColor, 0.0);
    surface.alpha = saturate(inputs.alpha);
    const float3 fallbackNormal = PrincipledUnitNormal(geometricNormal, float3(0.0, 0.0, 1.0));
    surface.normal = PrincipledUnitNormal(inputs.normal, fallbackNormal);
    surface.roughness = saturate(inputs.roughness);
    surface.metallic = saturate(inputs.metallic);
    surface.emission = inputs.emissionColor * inputs.emissionStrength;
    surface.occlusion = saturate(inputs.occlusion);
    surface.ior = max(inputs.ior, 1e-5);
    surface.specularIorLevel = max(inputs.specularIorLevel, 0.0);

    const float reflectivity = (surface.ior - 1.0) / (surface.ior + 1.0);
    float dielectricF0 = reflectivity * reflectivity;
    if (surface.specularIorLevel != 0.5)
    {
        dielectricF0 *= 2.0 * surface.specularIorLevel;
        const float rootF0 = sqrt(clamp(dielectricF0, 0.0, 0.99));
        const float adjustedIor = (1.0 + rootF0) / (1.0 - rootF0);
        surface.ior = surface.ior < 1.0 ? rcp(adjustedIor) : adjustedIor;
    }
    surface.dielectricF0 = saturate(dielectricF0 * max(inputs.specularTint, 0.0));
    surface.metalF0 = saturate(surface.baseColor);

    // F82 Tint changes the Schlick curve around cos(theta)=1/7, leaving
    // normal incidence and grazing reflection unchanged. It is not an F0 tint.
    const float cosine82 = 1.0 / 7.0;
    const float x82 = 1.0 - cosine82;
    const float x82Squared = x82 * x82;
    const float schlick82 = x82Squared * x82Squared * x82;
    const float3 reflection82 = lerp(surface.metalF0, float3(1.0), schlick82);
    surface.metalF82Correction = reflection82 * (1.0 - saturate(inputs.specularTint))
        / (cosine82 * schlick82 * x82);
    surface.coatWeight = 0.0;
    surface.coatRoughness = saturate(inputs.coatRoughness);
    surface.coatIor = max(inputs.coatIor, 1.0);
    surface.coatTint = max(inputs.coatTint, 0.0);
    surface.coatNormal = PrincipledUnitNormal(inputs.coatNormal, fallbackNormal);
    surface.sheenWeight = 0.0;
    surface.sheenRoughness = clamp(inputs.sheenRoughness, 1e-3, 1.0);
    surface.sheenTint = max(inputs.sheenTint, 0.0);
    surface.anisotropy = 0.0;
    surface.anisotropyRotation = frac(inputs.anisotropyRotation);
    surface.thinFilmThickness = 0.0;
    surface.thinFilmIor = max(inputs.thinFilmIor, 1e-5);
#if (LX_MATERIAL_FEATURE_MASK & 0x0080u) != 0
    surface.coatWeight = max(inputs.coatWeight, 0.0);
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0100u) != 0
    surface.sheenWeight = max(inputs.sheenWeight, 0.0);
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0200u) != 0
    surface.anisotropy = saturate(inputs.anisotropy);
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    surface.thinFilmThickness = max(inputs.thinFilmThickness, 0.0);
#endif

    const float3 axis = abs(surface.normal.z) < 0.999
        ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    const float3 axisTangent = normalize(cross(axis, surface.normal));
    const float3 fallbackTangent = PrincipledUnitNormal(geometricTangent
        - surface.normal * dot(geometricTangent, surface.normal), axisTangent);
    const float3 tangent = PrincipledUnitNormal(inputs.tangent
        - surface.normal * dot(inputs.tangent, surface.normal), fallbackTangent);
    const float angle = surface.anisotropyRotation * 6.283185307179586;
    surface.tangent = tangent * cos(angle) + cross(surface.normal, tangent) * sin(angle);
    surface.transmissionWeight = 0.0;
    // Glass uses authored IOR, independent of the base Specular IOR Level.
    surface.transmissionIor = max(inputs.ior, 1e-5);
    const float glassReflectivity = (surface.transmissionIor - 1.0) / (surface.transmissionIor + 1.0);
    surface.transmissionF0 = saturate(glassReflectivity * glassReflectivity * max(inputs.specularTint, 0.0));
    surface.transmissionFilmIor = surface.thinFilmIor;
    surface.subsurfaceWeight = 0.0;
    surface.subsurfaceRadius = max(inputs.subsurfaceRadius, 0.0);
    surface.subsurfaceScale = max(inputs.subsurfaceScale, 0.0);
    surface.subsurfaceIor = max(inputs.subsurfaceIor, 1.0);
    surface.subsurfaceAnisotropy = clamp(inputs.subsurfaceAnisotropy, 0.0, 0.99);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    surface.transmissionWeight = saturate(inputs.transmissionWeight);
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x1000u) != 0
    surface.subsurfaceWeight = saturate(inputs.subsurfaceWeight);
#endif
    return surface;
}

// The fixed glTF path has different defaults and preserves factor products in
// the MRTs. Keep that adapter explicit instead of weakening Principled clamps.
MaterialInputs DefaultStandardMaterialInputs()
{
    MaterialInputs inputs = DefaultMaterialInputs();
    inputs.baseColor = float3(1.0);
    inputs.roughness = 1.0;
    return inputs;
}

PrincipledSurface EvaluateStandardMaterial(MaterialInputs inputs)
{
    PrincipledSurface surface = EvaluateMaterial(inputs);
    surface.baseColor = inputs.baseColor;
    surface.alpha = inputs.alpha;
    surface.normal = inputs.normal;
    surface.roughness = inputs.roughness;
    surface.metallic = inputs.metallic;
    surface.occlusion = inputs.occlusion;
    surface.dielectricF0 = float3(0.04);
    surface.metalF0 = inputs.baseColor;
    surface.metalF82Correction = float3(0.0);
    return surface;
}

// Deferred reconstructs the logical Standard surface from existing MRTs.
// Higher tiers require a route/packing decision in MAT-7.
PrincipledSurface UnpackStandardSurface(float4 baseColor, float4 orm,
    float3 normal, float3 emission)
{
    MaterialInputs inputs = DefaultStandardMaterialInputs();
    inputs.baseColor = baseColor.rgb;
    inputs.alpha = baseColor.a;
    inputs.normal = normal;
    inputs.roughness = orm.g;
    inputs.metallic = orm.b;
    inputs.emissionColor = emission;
    inputs.emissionStrength = 1.0;
    inputs.occlusion = orm.r;
    return EvaluateStandardMaterial(inputs);
}
