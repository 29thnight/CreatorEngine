#pragma once

#include "SpecialMaterialRoute.slang"
#include "PrincipledLayered.slang"
#include "PrincipledSubsurface.slang"
#include "PrincipledVolume.slang"

PrincipledSurface OrientSpecialSurface(PrincipledSurface material, bool frontFace)
{
    if (!frontFace)
    {
        material.normal = -material.normal;
        material.coatNormal = -material.coatNormal;
        material.transmissionFilmIor /= material.transmissionIor;
        material.transmissionIor = rcp(material.transmissionIor);
    }
    return material;
}

PrincipledSurface SpecialMetal(PrincipledSurface material)
{
    material.metallic = 1.0;
    return material;
}

PrincipledSurface SpecialDielectric(PrincipledSurface material)
{
    material.metallic = 0.0;
    return material;
}

PrincipledSurface SpecialGlass(PrincipledSurface material)
{
    material.metallic = 0.0;
    material.ior = material.transmissionIor;
    material.dielectricF0 = material.transmissionF0;
    material.thinFilmIor = material.transmissionFilmIor;
    // Principled glass is isotropic in the pinned Cycles composition.
    material.anisotropy = 0.0;
    return material;
}

float TransmissionVisibility(float3 view, float3 light, PrincipledSurface material)
{
    const float3 frontLight = light - 2.0 * dot(material.normal, light) * material.normal;
    const float alpha = max(material.roughness * material.roughness, 1e-3);
    return PrincipledAnisotropicVisibility(view, frontLight, material.normal, material.tangent,
        cross(material.normal, material.tangent), float2(alpha));
}

struct SpecialForwardIntegral
{
    PrincipledLayeredIntegral layers;
    PrincipledIblIntegral metal;
    PrincipledIblIntegral dielectric;
    PrincipledIblIntegral glass;
    float3 rawTransmission;
};

// Bake-only quadrature of the same Walter GGX refraction used for direct light.
// Transmission is energy-normalized for the game's single-interface environment
// model; it does not implement Cycles path transport through a closed solid.
float3 IntegrateSpecialTransmission(PrincipledSurface glass, float3 view)
{
    const float nv = saturate(dot(glass.normal, view));
    const PrincipledReflection reflection = BuildPrincipledReflection(glass);
    if (nv <= 0.0)
    {
        return float3(0.0);
    }
    if (glass.ior == 1.0 || glass.roughness == 0.0)
    {
        return 1.0 - EvaluatePrincipledFresnel(reflection, nv);
    }
    const float alpha = max(glass.roughness * glass.roughness, 1e-3);
    const float3 bitangent = cross(glass.normal, glass.tangent);
    float3 transmission = float3(0.0);
    for (uint index = 0u; index < 1024u; ++index)
    {
        const float3 halfVector = PrincipledSampleAnisotropic(Hammersley(index, 1024u), glass.normal,
            glass.tangent, bitangent, float2(alpha));
        const float vh = dot(view, halfVector);
        if (vh <= 0.0)
        {
            continue;
        }
        const float3 light = refract(-view, halfVector, rcp(glass.ior));
        const float nl = -dot(glass.normal, light);
        if (nl > 0.0)
        {
            const float weight = 4.0 * nl * TransmissionVisibility(view, light, glass) * vh
                / max(dot(glass.normal, halfVector), 1e-6);
            transmission += (1.0 - EvaluatePrincipledFresnel(reflection, vh)) * weight;
        }
    }
    return max(transmission / 1024.0, 0.0);
}

SpecialForwardIntegral IntegrateSpecialForward(PrincipledSurface material, float3 view)
{
    SpecialForwardIntegral integral;
    integral.layers = IntegrateLayeredIbl(material, view);
    integral.metal = IntegrateLayeredReflection(SpecialMetal(material), view, false);
    integral.dielectric = IntegrateLayeredReflection(SpecialDielectric(material), view, false);
    integral.glass = PrincipledEmptyIntegral();
    integral.rawTransmission = float3(0.0);
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    if (material.transmissionWeight > 0.0 && material.metallic < 1.0)
    {
        const PrincipledSurface glass = SpecialGlass(material);
        // Glass retains its separate single-interface transmission budget.
        integral.glass = IntegrateLayeredReflection(glass, view, false, false);
        integral.rawTransmission = IntegrateSpecialTransmission(glass, view);
    }
#endif
    return integral;
}

struct SpecialForwardSurface
{
    PrincipledLayeredState layers;
    PrincipledSurface glass;
    SpecialForwardIntegral integral;
    PrincipledReflection metalReflection;
    PrincipledReflection dielectricReflection;
    PrincipledReflection glassReflection;
    float3 metalMultiple;
    float3 dielectricMultiple;
    float3 glassMultiple;
    float3 diffuse;
    float3 subsurface;
    float3 transmission;
    float3 transmissionNormalization;
    float glassWeight;
    PrincipledSubsurfaceProfile profile;
};

SpecialForwardSurface BuildSpecialForwardSurface(PrincipledSurface material, float3 view,
    SpecialForwardIntegral integral)
{
    SpecialForwardSurface surface;
    surface.layers = BuildLayeredState(material, view, integral.layers);
    surface.glass = SpecialGlass(material);
    surface.integral = integral;
    surface.metalReflection = BuildPrincipledReflection(SpecialMetal(material));
    surface.dielectricReflection = BuildPrincipledReflection(SpecialDielectric(material));
    surface.glassReflection = BuildPrincipledReflection(surface.glass);
    surface.metalMultiple = float3(0.0);
    surface.dielectricMultiple = float3(0.0);
    surface.glassMultiple = PrincipledMultipleScatter(integral.glass);
    const float remaining = max(1.0 - PrincipledMaxChannel(surface.layers.energy.dielectricAlbedo), 0.0);
    const float diffuseWeight = (1.0 - material.metallic) * (1.0 - material.transmissionWeight) * remaining;
    surface.diffuse = material.baseColor * diffuseWeight * (1.0 - material.subsurfaceWeight);
    surface.subsurface = saturate(material.baseColor) * diffuseWeight * material.subsurfaceWeight;
    surface.glassWeight = (1.0 - material.metallic) * material.transmissionWeight;
    const float3 glassBudget = max(1.0 - integral.glass.singleScatter - surface.glassMultiple, 0.0);
    surface.transmission = surface.glassWeight * sqrt(saturate(material.baseColor)) * glassBudget;
    surface.transmissionNormalization = select(integral.rawTransmission > 1e-8,
        surface.transmission / max(integral.rawTransmission, 1e-8), float3(0.0));
    surface.profile = BuildSubsurfaceProfile(material);
    return surface;
}

float3 EvaluateSpecialTransmission(SpecialForwardSurface surface, float3 light)
{
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    const PrincipledSurface glass = surface.glass;
    const float3 view = surface.layers.view;
    const float nl = -dot(glass.normal, light);
    if (nl <= 0.0 || dot(glass.normal, view) <= 0.0 || glass.ior == 1.0 || surface.glassWeight == 0.0)
    {
        return float3(0.0);
    }
    float3 halfVector = PrincipledUnitNormal(view + glass.ior * light, glass.normal);
    halfVector *= dot(halfVector, glass.normal) < 0.0 ? -1.0 : 1.0;
    const float vh = dot(view, halfVector);
    const float lh = dot(light, halfVector);
    if (vh <= 0.0 || lh >= 0.0)
    {
        return float3(0.0);
    }
    const float alpha = max(glass.roughness * glass.roughness, 1e-3);
    const float denominator = vh + glass.ior * lh;
    const float distribution = DistributionGGX(saturate(dot(glass.normal, halfVector)), alpha);
    const float factor = 4.0 * distribution * TransmissionVisibility(view, light, glass)
        * glass.ior * glass.ior * vh * abs(lh) * nl / max(denominator * denominator, 1e-12);
    return surface.layers.lowerTransmission * surface.transmissionNormalization
        * (1.0 - EvaluatePrincipledFresnel(surface.glassReflection, vh)) * factor;
#else
    return float3(0.0);
#endif
}

struct SpecialForwardLight
{
    float3 local;
    float3 subsurfaceSource;
    float3 transmission;
};

SpecialForwardLight EvaluateSpecialLight(SpecialForwardSurface surface, float3 toLight, float3 radiance)
{
    const PrincipledSurface material = surface.layers.material;
    const float3 light = PrincipledUnitNormal(toLight, material.normal);
    const float3 view = surface.layers.view;
    const float3 halfVector = PrincipledUnitNormal(view + light, material.normal);
    const float nl = saturate(dot(material.normal, light));
    const float vh = saturate(dot(view, halfVector));
    SpecialForwardLight result;
    // Isolate the existing Coat/Sheen lobes; the base is split below.
    PrincipledLayeredState top = surface.layers;
    top.reflection.ior = 1.0;
    top.reflection.metallic = 0.0;
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    top.reflection.thinFilmThickness = 0.0;
#endif
    top.baseMultipleScatter = float3(0.0);
    top.baseDiffuse = float3(0.0);
    result.local = EvaluateLayeredLight(top, light, radiance);
    result.subsurfaceSource = float3(0.0);
    result.transmission = EvaluateSpecialTransmission(surface, light) * radiance;
    if (nl > 0.0 && dot(material.normal, view) > 0.0)
    {
        const float distribution = PrincipledAnisotropicDistribution(halfVector, material.normal,
            surface.layers.tangent, surface.layers.bitangent, surface.layers.alpha);
        const float visibility = PrincipledAnisotropicVisibility(view, light, material.normal,
            surface.layers.tangent, surface.layers.bitangent, surface.layers.alpha);
        const float dielectricWeight = (1.0 - material.metallic) * (1.0 - material.transmissionWeight);
        float3 reflection = distribution * visibility
            * (material.metallic * EvaluatePrincipledFresnel(surface.metalReflection, vh) * surface.layers.energy.metal
                + dielectricWeight * EvaluatePrincipledFresnel(surface.dielectricReflection, vh) * surface.layers.energy.dielectric);
        const float glassAlpha = max(material.roughness * material.roughness, 1e-3);
        reflection += surface.glassWeight * EvaluatePrincipledFresnel(surface.glassReflection, vh)
            * DistributionGGX(saturate(dot(material.normal, halfVector)), glassAlpha)
            * VisibilitySmithHeightCorrelated(saturate(dot(material.normal, view)), nl, glassAlpha);
        const float3 multiple = material.metallic * surface.metalMultiple
            + dielectricWeight * surface.dielectricMultiple + surface.glassWeight * surface.glassMultiple;
        result.local += surface.layers.lowerTransmission * nl * radiance
            * (reflection + (multiple + surface.diffuse) * INV_PI);
        if (any(surface.subsurface > 0.0))
        {
            // Incident irradiance/PI for redistribution. The destination applies
            // its SSS color/weight once when consuming the prepared irradiance.
            result.subsurfaceSource = surface.layers.lowerTransmission * nl * radiance * INV_PI;
        }
    }
    return result;
}

struct SpecialForwardEnvironment
{
    float3 irradiance;
    float3 basePrefiltered;
    float3 glassPrefiltered;
    float3 coatPrefiltered;
    float3 sheenIrradiance;
    float3 refractedRadiance;
    float3 subsurfaceIrradiance;
};

struct SpecialForwardAmbient
{
    float3 local;
    float3 subsurface;
    float3 transmission;
    float3 emission;

    float3 Sum()
    {
        return local + subsurface + transmission;
    }
};

SpecialForwardAmbient EvaluateSpecialIbl(SpecialForwardSurface surface, SpecialForwardEnvironment environment)
{
    const PrincipledSurface material = surface.layers.material;
    const float dielectricWeight = (1.0 - material.metallic) * (1.0 - material.transmissionWeight);
    const float ao = ComputeSpecularOcclusion(saturate(dot(material.normal, surface.layers.view)),
        material.occlusion, material.roughness);
    const PrincipledLayeredAmbient top = EvaluateLayeredIbl(surface.layers, environment.irradiance,
        environment.basePrefiltered, environment.coatPrefiltered, environment.sheenIrradiance);
    const float3 normalization = min(float3(1.0), max(1.0 - surface.layers.baseDiffuse, 0.0)
        / max(surface.integral.layers.base.singleScatter, 1e-20));
    const float3 baseReflection = normalization * (material.metallic * surface.integral.metal.singleScatter
        + dielectricWeight * surface.integral.dielectric.singleScatter);
    const float3 multiple = material.metallic * surface.metalMultiple
        + dielectricWeight * surface.dielectricMultiple + surface.glassWeight * surface.glassMultiple;
    SpecialForwardAmbient ambient;
    ambient.local = top.coat + top.sheen + surface.layers.lowerTransmission
        * ((baseReflection * environment.basePrefiltered
                + surface.glassWeight * surface.integral.glass.singleScatter * environment.glassPrefiltered) * ao
            + (surface.diffuse + multiple) * environment.irradiance * material.occlusion);
    const float3 subsurfaceIrradiance = select(surface.profile.local,
        environment.irradiance, environment.subsurfaceIrradiance);
    ambient.subsurface = surface.layers.lowerTransmission * surface.subsurface * subsurfaceIrradiance;
    // Transmission and nonlocal SSS have their own visibility, not local AO.
    ambient.transmission = surface.layers.lowerTransmission * surface.transmission * environment.refractedRadiance;
    ambient.emission = top.emission;
    return ambient;
}

float3 CompositeSpecialForward(float3 surfaceRadiance, float alpha, float3 background, VolumeSegment medium)
{
    const float3 surface = lerp(background, surfaceRadiance, saturate(alpha));
#if (LX_MATERIAL_FEATURE_MASK & 0x2000u) != 0
    return medium.radiance + medium.transmittance * surface;
#else
    return surface;
#endif
}
