#pragma once

#include "PrincipledSurface.slang"
#if (LX_MATERIAL_FEATURE_MASK & 0x1800u) != 0 || ((LX_MATERIAL_FEATURE_MASK & 0x2000u) != 0 && !defined(LX_SCENE_VOLUME_TRANSPORT))
#error Special features require SpecialForwardSurface, with split transmission and nonlocal subsurface lighting.
#endif
#include "Ibl.slang"
#include "PrincipledBrdf.slang"
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
#include "PrincipledLayered.slang"
#endif

// Shared direct-light surface for Deferred and Forward. Texture bindings and
// light selection belong to each pass; the current GGX evaluation belongs here.
struct PbrSurface
{
    float3 position;
    float3 normal;
    float3 view;
    float ndotv;
    float alpha;
    float3 f0;
    float3 diffuseBase;
    float3 energyComp;
#if (LX_MATERIAL_FEATURE_MASK & 0x0040u) != 0
    PrincipledReflection reflection;
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
    PrincipledLayeredState layered;
#endif

    float3 EvaluateLight(float3 toLight, float3 radiance)
    {
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
        return EvaluateLayeredLight(layered, toLight, radiance);
#else
        const float ndotl = saturate(dot(normal, toLight));
        if (ndotl <= 0.0)
        {
            return float3(0.0);
        }

        const float3 halfVector = normalize(toLight + view);
        const float ndoth = saturate(dot(normal, halfVector));
        const float vdoth = saturate(dot(view, halfVector));
#if (LX_MATERIAL_FEATURE_MASK & 0x0040u) != 0
        const float3 fresnel = EvaluatePrincipledFresnel(reflection, vdoth);
#else
        const float3 fresnel = FresnelSchlick(f0, vdoth);
#endif
        const float3 specular = fresnel * energyComp
            * DistributionGGX(ndoth, alpha)
            * VisibilitySmithHeightCorrelated(ndotv, ndotl, alpha);
        const float3 diffuse = (1.0 - fresnel) * diffuseBase;
        return (diffuse + specular) * radiance * ndotl;
#endif
    }
};

float PbrNdotV(float3 normal, float3 view)
{
    return saturate(dot(normal, view)) + 1e-5;
}

#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
PbrSurface BuildPbrSurface(float3 position, float3 view, PrincipledSurface material,
    float2 dfg, PrincipledLayeredIntegral layeredIntegral)
#else
PbrSurface BuildPbrSurface(float3 position, float3 view, PrincipledSurface material, float2 dfg)
#endif
{
    const float roughness = saturate(material.roughness);
    const float metallic = saturate(material.metallic);
    PbrSurface surface;
    surface.position = position;
    surface.normal = material.normal;
    surface.view = view;
    surface.ndotv = PbrNdotV(material.normal, view);
    surface.alpha = max(roughness * roughness, 1e-3);
    surface.f0 = lerp(material.dielectricF0, material.metalF0, metallic);
    surface.diffuseBase = material.baseColor * (1.0 - metallic) * INV_PI;
    surface.energyComp = SpecularEnergyCompensation(surface.f0, dfg);
#if (LX_MATERIAL_FEATURE_MASK & 0x0040u) != 0
    surface.reflection = BuildPrincipledReflection(material);
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
    surface.layered = BuildLayeredState(material, view, layeredIntegral);
#endif
    return surface;
}

IblAmbient EvaluateStandardIbl(PrincipledSurface material,
    PbrSurface surface, float3 irradiance, float3 prefiltered, float2 dfg)
{
    return EvaluateIblAmbient(material.baseColor, surface.f0,
        saturate(material.roughness), saturate(material.metallic),
        surface.ndotv, material.occlusion, irradiance, prefiltered, dfg);
}

// No IBL set means no BRDF LUT is bound. (1, 0) disables LUT-derived energy
// compensation while preserving direct light; a null sample (0, 0) amplifies it.
float2 PbrNeutralDfg()
{
    return float2(1.0, 0.0);
}

float PbrLocalLightFalloff(float4 attenuation, float distance)
{
    const float denominator = attenuation.x + attenuation.y * distance
        + attenuation.z * distance * distance;
    return saturate(1.0 - distance / max(attenuation.w, 1e-4))
        / max(denominator, 1e-6);
}

float PbrSpotFalloff(float3 toLight, float4 direction)
{
    const float cosAngle = dot(-toLight, normalize(direction.xyz));
    const float cutoff = cos(direction.w * 0.5);
    return smoothstep(cutoff, lerp(cutoff, 1.0, 0.2), cosAngle);
}
