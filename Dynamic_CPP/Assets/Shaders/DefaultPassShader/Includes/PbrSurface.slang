#pragma once

#include "Ibl.slang"

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

    float3 EvaluateLight(float3 toLight, float3 radiance)
    {
        const float ndotl = saturate(dot(normal, toLight));
        if (ndotl <= 0.0) return float3(0.0);

        const float3 halfVector = normalize(toLight + view);
        const float ndoth = saturate(dot(normal, halfVector));
        const float vdoth = saturate(dot(view, halfVector));
        const float3 fresnel = FresnelSchlick(f0, vdoth);
        const float3 specular = fresnel * energyComp
            * DistributionGGX(ndoth, alpha)
            * VisibilitySmithHeightCorrelated(ndotv, ndotl, alpha);
        const float3 diffuse = (1.0 - fresnel) * diffuseBase;
        return (diffuse + specular) * radiance * ndotl;
    }
};

float PbrNdotV(float3 normal, float3 view)
{
    return saturate(dot(normal, view)) + 1e-5;
}

PbrSurface BuildPbrSurface(float3 position, float3 normal, float3 view,
    float3 albedo, float roughness, float metallic, float2 dfg)
{
    PbrSurface surface;
    surface.position = position;
    surface.normal = normal;
    surface.view = view;
    surface.ndotv = PbrNdotV(normal, view);
    surface.alpha = max(roughness * roughness, 1e-3);
    surface.f0 = lerp(float3(0.04), albedo, metallic);
    surface.diffuseBase = albedo * (1.0 - metallic) * INV_PI;
    surface.energyComp = SpecularEnergyCompensation(surface.f0, dfg);
    return surface;
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
