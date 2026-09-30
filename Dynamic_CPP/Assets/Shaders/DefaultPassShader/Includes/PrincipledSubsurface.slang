#pragma once

#include "PrincipledSurface.slang"

// Normalized diffusion dipole: a game approximation, not Cycles Random Walk.
// Radius*Scale is interpreted as per-channel mean free path in world units.
struct PrincipledSubsurfaceProfile
{
    float3 realDepth;
    float3 virtualDepth;
    float3 attenuation;
    float3 normalization;
    bool3 local;
};

PrincipledSubsurfaceProfile BuildSubsurfaceProfile(PrincipledSurface material)
{
    const float3 radius = material.subsurfaceRadius * material.subsurfaceScale;
    const float3 albedo = saturate(material.baseColor);
    const float3 extinction = rcp(max(radius, 1e-6));
    const float3 absorption = extinction * (1.0 - albedo);
    const float3 reducedScattering = extinction * albedo * (1.0 - material.subsurfaceAnisotropy);
    const float3 reducedExtinction = max(absorption + reducedScattering, 1e-8);
    const float3 diffusion = rcp(3.0 * reducedExtinction);
    const float eta = material.subsurfaceIor;
    const float fresnelDiffuse = clamp(-1.4399 / (eta * eta) + 0.7099 / eta + 0.6681 + 0.0636 * eta, 0.0, 0.99);
    const float boundary = (1.0 + fresnelDiffuse) / (1.0 - fresnelDiffuse);
    PrincipledSubsurfaceProfile profile;
    profile.realDepth = rcp(reducedExtinction);
    profile.virtualDepth = profile.realDepth + 4.0 * boundary * diffusion;
    profile.attenuation = sqrt(absorption / diffusion);
    profile.normalization = exp(-profile.attenuation * profile.realDepth)
        + exp(-profile.attenuation * profile.virtualDepth);
    profile.local = radius <= 0.0 || albedo <= 0.0;
    return profile;
}

float3 SubsurfaceDipoleTerm(float3 depth, float3 attenuation, float distance)
{
    const float3 length = sqrt(depth * depth + distance * distance);
    return depth * (attenuation + rcp(length)) * exp(-attenuation * length) / (length * length);
}

float3 EvaluateSubsurfaceProfile(PrincipledSubsurfaceProfile profile, float distance)
{
    const float radius = max(distance, 0.0);
    const float3 value = (SubsurfaceDipoleTerm(profile.realDepth, profile.attenuation, radius)
        + SubsurfaceDipoleTerm(profile.virtualDepth, profile.attenuation, radius))
        / max(6.283185307179586 * profile.normalization, 1e-20);
    return select(profile.local, float3(0.0), value);
}

// Exact radial cumulative integral, useful for bounded sampling and validation.
float3 SubsurfaceProfileCdf(PrincipledSubsurfaceProfile profile, float distance)
{
    const float radius = max(distance, 0.0);
    const float3 realLength = sqrt(profile.realDepth * profile.realDepth + radius * radius);
    const float3 virtualLength = sqrt(profile.virtualDepth * profile.virtualDepth + radius * radius);
    const float3 remaining = profile.realDepth / realLength * exp(-profile.attenuation * realLength)
        + profile.virtualDepth / virtualLength * exp(-profile.attenuation * virtualLength);
    const float3 cumulative = saturate(1.0 - remaining / max(profile.normalization, 1e-20));
    return select(profile.local, float3(1.0), cumulative);
}
