#pragma once

#include "SpecialMaterialRoute.slang"

// Volume is a separate Material Output closure, not a density on a BSDF.
struct VolumeInputs
{
    float3 color;
    float density;
    float3 absorptionColor;
    float anisotropy;
    float3 emissionColor;
    float emissionStrength;
};

struct PrincipledVolume
{
    float3 scattering;
    float3 absorption;
    float3 emission;
    float anisotropy;
};

VolumeInputs DefaultVolumeInputs()
{
    VolumeInputs inputs;
    inputs.color = float3(0.5);
    inputs.density = 1.0;
    inputs.absorptionColor = float3(0.0);
    inputs.anisotropy = 0.0;
    inputs.emissionColor = float3(1.0);
    inputs.emissionStrength = 0.0;
    return inputs;
}

PrincipledVolume EvaluateVolume(VolumeInputs inputs)
{
    PrincipledVolume volume;
    volume.scattering = float3(0.0);
    volume.absorption = float3(0.0);
    volume.emission = float3(0.0);
    volume.anisotropy = 0.0;
#if (LX_MATERIAL_FEATURE_MASK & 0x2000u) != 0
    const float density = max(inputs.density, 0.0);
    const float3 color = max(inputs.color, 0.0);
    const float3 absorption = sqrt(max(inputs.absorptionColor, 0.0));
    if (density > 1e-5)
    {
        volume.scattering = color * density;
        volume.absorption = max(1.0 - color, 0.0) * max(1.0 - absorption, 0.0) * density;
    }
    const float strength = max(inputs.emissionStrength, 0.0);
    volume.emission = strength > 1e-5 ? max(inputs.emissionColor, 0.0) * strength : float3(0.0);
    volume.anisotropy = clamp(inputs.anisotropy, -0.999, 0.999);
#endif
    return volume;
}

float VolumePhase(float cosine, float anisotropy)
{
    const float g = clamp(anisotropy, -0.999, 0.999);
    // (1-g)^2+2g(1-cos) is stable near the forward peak.
    const float c = clamp(cosine, -1.0, 1.0);
    const float denominator = g >= 0.0 ? (1.0 - g) * (1.0 - g) + 2.0 * g * (1.0 - c)
        : (1.0 + g) * (1.0 + g) - 2.0 * g * (1.0 + c);
    return (1.0 - g * g) / (12.566370614359172 * denominator * sqrt(denominator));
}

struct VolumeSegment
{
    float3 transmittance;
    float3 radiance;
};

VolumeSegment IntegrateVolumeSegment(PrincipledVolume volume, float distance, float3 incidentSource)
{
    const float length = max(distance, 0.0);
    const float3 extinction = volume.scattering + volume.absorption;
    const float3 tau = extinction * length;
    VolumeSegment segment;
    segment.transmittance = exp(-tau);
    const float3 polynomial = length * (1.0 - tau * 0.5 + tau * tau / 6.0 - tau * tau * tau / 24.0);
    const float3 integral = select(tau < 1e-3, polynomial,
        (1.0 - segment.transmittance) / max(extinction, 1e-20));
    segment.radiance = integral * (volume.scattering * incidentSource + volume.emission);
    return segment;
}

VolumeSegment ComposeVolumeSegments(VolumeSegment first, VolumeSegment second)
{
    VolumeSegment combined;
    combined.transmittance = first.transmittance * second.transmittance;
    combined.radiance = first.radiance + first.transmittance * second.radiance;
    return combined;
}
