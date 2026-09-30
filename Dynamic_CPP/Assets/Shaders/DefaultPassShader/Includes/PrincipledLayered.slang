#pragma once

#include "PrincipledBrdf.slang"
#if (LX_MATERIAL_FEATURE_MASK & 0x0100u) != 0
#include "SheenLtc.slang"
#endif

float PrincipledMaxChannel(float3 value)
{
    return max(value.r, max(value.g, value.b));
}

float3 PrincipledProjectedTangent(float3 tangent, float3 normal)
{
    const float3 axis = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return PrincipledUnitNormal(tangent - normal * dot(tangent, normal), normalize(cross(axis, normal)));
}

float2 PrincipledAnisotropicAlpha(PrincipledSurface material)
{
    const float alpha = max(material.roughness * material.roughness, 1e-3);
    const float aspect = sqrt(1.0 - 0.9 * material.anisotropy);
    return float2(alpha / aspect, alpha * aspect);
}

float PrincipledAnisotropicDistribution(float3 halfVector, float3 normal,
    float3 tangent, float3 bitangent, float2 alpha)
{
    const float3 projected = float3(dot(halfVector, tangent) / alpha.x,
        dot(halfVector, bitangent) / alpha.y, dot(halfVector, normal));
    const float denominator = dot(projected, projected);
    return rcp(max(PI * alpha.x * alpha.y * denominator * denominator, 1e-20));
}

float PrincipledAnisotropicVisibility(float3 view, float3 toLight,
    float3 normal, float3 tangent, float3 bitangent, float2 alpha)
{
    const float nv = saturate(dot(normal, view));
    const float nl = saturate(dot(normal, toLight));
    const float vx = alpha.x * dot(tangent, view);
    const float vy = alpha.y * dot(bitangent, view);
    const float lx = alpha.x * dot(tangent, toLight);
    const float ly = alpha.y * dot(bitangent, toLight);
    const float viewLength = sqrt(vx * vx + vy * vy + nv * nv);
    const float lightLength = sqrt(lx * lx + ly * ly + nl * nl);
    return 0.5 / max(nl * viewLength + nv * lightLength, 1e-6);
}

float3 PrincipledSampleAnisotropic(float2 xi, float3 normal,
    float3 tangent, float3 bitangent, float2 alpha)
{
    const float angle = 2.0 * PI * xi.x;
    const float radius = sqrt(xi.y);
    const float3 local = normalize(float3(alpha.x * radius * cos(angle),
        alpha.y * radius * sin(angle), sqrt(1.0 - xi.y)));
    return local.x * tangent + local.y * bitangent + local.z * normal;
}

float3 PrincipledSheenLtc(float ndotv, float roughness)
{
#if (LX_MATERIAL_FEATURE_MASK & 0x0100u) != 0
    const float2 coordinate = saturate(float2(ndotv, roughness)) * 31.0;
    const uint2 lower = uint2(floor(coordinate));
    const uint2 upper = min(lower + uint2(1u), uint2(31u));
    const float2 fraction = frac(coordinate);
    const float3 first = lerp(kPrincipledSheenLtc[lower.y * 32u + lower.x],
        kPrincipledSheenLtc[lower.y * 32u + upper.x], fraction.x);
    const float3 second = lerp(kPrincipledSheenLtc[upper.y * 32u + lower.x],
        kPrincipledSheenLtc[upper.y * 32u + upper.x], fraction.x);
    const float3 coefficients = lerp(first, second, fraction.y);
    return abs(coefficients.x) > 1e-5 && coefficients.z > 1e-5
        ? coefficients : float3(1.0, 0.0, 0.0);
#else
    return float3(1.0, 0.0, 0.0);
#endif
}

float3 PrincipledCoatFresnel(PrincipledSurface material, float cosine)
{
    // Coat has a physical dielectric interface, without base Specular Level,
    // F0 tint or thin film. Preserve its dip below F0 for higher IORs.
    return float3(PrincipledDielectricFresnel(cosine, material.coatIor));
}

PrincipledIblIntegral PrincipledEmptyIntegral()
{
    PrincipledIblIntegral integral;
    integral.singleScatter = float3(0.0);
    integral.directionalAlbedo = 1.0;
    integral.averageFresnel = float3(0.0);
    return integral;
}

// Bake-only hemisphere integral of the same correlated-Smith GGX used below.
// The view azimuth is required for anisotropy; a 2D isotropic DFG is insufficient.
PrincipledIblIntegral IntegrateLayeredReflection(PrincipledSurface material, float3 view, bool coat)
{
    const float3 normal = coat ? material.coatNormal : material.normal;
    const float3 tangent = PrincipledProjectedTangent(material.tangent, normal);
    const float3 bitangent = cross(normal, tangent);
    const float roughness = coat ? material.coatRoughness : material.roughness;
    const float coatAlpha = max(roughness * roughness, 1e-3);
    const float2 alpha = coat ? float2(coatAlpha) : PrincipledAnisotropicAlpha(material);
    const PrincipledReflection reflection = BuildPrincipledReflection(material);
    PrincipledIblIntegral integral = PrincipledEmptyIntegral();
    const float nv = saturate(dot(normal, view));
    if (nv <= 0.0)
    {
        return integral;
    }
    integral.directionalAlbedo = 0.0;
    const uint samples = 1024u;
    if (roughness == 0.0)
    {
        integral.singleScatter = coat ? PrincipledCoatFresnel(material, nv)
            : EvaluatePrincipledFresnel(reflection, nv);
        integral.directionalAlbedo = 1.0;
    }
    else
    {
        for (uint index = 0u; index < samples; ++index)
        {
            const float3 halfVector = PrincipledSampleAnisotropic(Hammersley(index, samples),
                normal, tangent, bitangent, alpha);
            const float vh = saturate(dot(view, halfVector));
            const float3 toLight = 2.0 * vh * halfVector - view;
            const float nl = saturate(dot(normal, toLight));
            if (nl > 0.0)
            {
                const float visibility = PrincipledAnisotropicVisibility(view, toLight,
                    normal, tangent, bitangent, alpha);
                const float weight = 4.0 * nl * visibility * vh / max(dot(normal, halfVector), 1e-6);
                const float3 fresnel = coat ? PrincipledCoatFresnel(material, vh)
                    : EvaluatePrincipledFresnel(reflection, vh);
                integral.singleScatter += fresnel * weight;
                integral.directionalAlbedo += weight;
            }
        }
        integral.singleScatter /= float(samples);
        integral.directionalAlbedo = saturate(integral.directionalAlbedo / float(samples));
        integral.singleScatter = min(integral.singleScatter, integral.directionalAlbedo);
    }
    const uint averageSamples = 64u;
    for (uint index = 0u; index < averageSamples; ++index)
    {
        const float cosine = sqrt((float(index) + 0.5) / float(averageSamples));
        integral.averageFresnel += coat ? PrincipledCoatFresnel(material, cosine)
            : EvaluatePrincipledFresnel(reflection, cosine);
    }
    integral.averageFresnel /= float(averageSamples);
    return integral;
}

struct PrincipledLayeredIntegral
{
    PrincipledIblIntegral base;
    PrincipledIblIntegral coat;
};

PrincipledLayeredIntegral IntegrateLayeredIbl(PrincipledSurface material, float3 view)
{
    const float3 unitView = PrincipledUnitNormal(view, material.normal);
    PrincipledLayeredIntegral integral;
    integral.base = IntegrateLayeredReflection(material, unitView, false);
    integral.coat = PrincipledEmptyIntegral();
#if (LX_MATERIAL_FEATURE_MASK & 0x0080u) != 0
    if (material.coatWeight > 0.0)
    {
        integral.coat = IntegrateLayeredReflection(material, unitView, true);
    }
#endif
    return integral;
}

float3 PrincipledMultipleScatter(PrincipledIblIntegral integral)
{
    const float missing = saturate(1.0 - integral.directionalAlbedo);
    return missing * integral.singleScatter * integral.averageFresnel
        / max(1.0 - missing * integral.averageFresnel, 1e-4);
}

float3 PrincipledCoatTransmission(PrincipledSurface material, float3 view)
{
    if (material.coatWeight <= 0.0)
    {
        return float3(1.0);
    }
    const float nv = saturate(dot(material.coatNormal, view));
    const float transmittedCosine = sqrt(max(1.0 - (1.0 - nv * nv)
        / (material.coatIor * material.coatIor), 0.0));
    // Linked HDR tint is allowed. Bound the exponent to finite float range;
    // a zero channel still absorbs completely rather than turning into epsilon.
    const float3 exponent = clamp(log2(max(material.coatTint, 1e-30))
        / max(transmittedCosine, 1e-4), -80.0, 80.0);
    const float3 absorption = select(material.coatTint > 0.0, exp2(exponent), float3(0.0));
    return lerp(float3(1.0), absorption, saturate(material.coatWeight));
}

struct PrincipledLayeredState
{
    PrincipledSurface material;
    float3 view;
    float3 tangent;
    float3 bitangent;
    float2 alpha;
    PrincipledReflection reflection;
    PrincipledLayeredIntegral integral;
    float3 baseMultipleScatter;
    float3 coatMultipleScatter;
    float3 baseDiffuse;
    float3 lowerTransmission;
    float sheenTransmission;
    float3 sheenNormal;
    float3 sheenTangent;
    float3 sheenBitangent;
    float3 sheenLtc;
    float3 sheenColor;
};

PrincipledLayeredState BuildLayeredState(PrincipledSurface material, float3 view,
    PrincipledLayeredIntegral integral)
{
    PrincipledLayeredState state;
    state.material = material;
    state.view = PrincipledUnitNormal(view, material.normal);
    state.tangent = material.tangent;
    state.bitangent = cross(material.normal, material.tangent);
    state.alpha = PrincipledAnisotropicAlpha(material);
    state.reflection = BuildPrincipledReflection(material);
    state.integral = integral;
    state.baseMultipleScatter = PrincipledMultipleScatter(integral.base);
    state.coatMultipleScatter = PrincipledMultipleScatter(integral.coat);
    const float3 baseReflectance = integral.base.singleScatter + state.baseMultipleScatter;
    state.baseDiffuse = material.baseColor * (1.0 - material.metallic)
        * max(1.0 - PrincipledMaxChannel(baseReflectance), 0.0);
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    if (material.thinFilmThickness > 0.0 && material.thinFilmIor != 1.0
        && (material.metallic > 0.0 || material.thinFilmIor != material.ior))
    {
        // Cycles GGX compensation uses the underlying closure's Fss even when
        // thin-film Fresnel is enabled. Its directional single scatter still
        // contains the film; only the average used for compensation is fixed.
        const float root = (state.reflection.ior - 1.0) / (state.reflection.ior + 1.0);
        const float physicalF0 = root * root;
        const float physicalFss = state.reflection.ior >= 1.0
            ? (state.reflection.ior - 1.0) / (4.08567 + 1.00071 * state.reflection.ior)
            : 1.0 - state.reflection.ior * state.reflection.ior
                * (1.0 - (rcp(state.reflection.ior) - 1.0) / (4.08567 + 1.00071 / state.reflection.ior));
        const float3 dielectricFss = lerp(state.reflection.dielectricF0,float3(1.0),
            saturate((physicalFss-physicalF0) / max(1.0-physicalF0,1e-20)));
        const float3 metalFss = lerp(state.reflection.metalF0,float3(1.0),1.0/21.0)
            - state.reflection.metalF82Correction / 126.0;
        PrincipledIblIntegral compensation = integral.base;
        compensation.averageFresnel = saturate(lerp(dielectricFss,metalFss,material.metallic));
        state.baseMultipleScatter = PrincipledMultipleScatter(compensation);
        // Cycles estimates a film dielectric closure's layering albedo at the
        // view angle. Metal is a separate closure, so its RGB reflectivity must
        // not attenuate the remaining dielectric diffuse a second time.
        PrincipledReflection dielectric = state.reflection;
        dielectric.metallic = 0.0;
        const float3 albedo = EvaluatePrincipledFresnel(dielectric,
            saturate(dot(material.normal, state.view)));
        state.baseDiffuse = material.baseColor * (1.0 - material.metallic)
            * max(1.0 - PrincipledMaxChannel(albedo), 0.0);
    }
#endif
    state.sheenNormal = PrincipledUnitNormal(lerp(material.normal, material.coatNormal,
        saturate(material.coatWeight)), material.normal);
    state.sheenTangent = PrincipledProjectedTangent(state.view, state.sheenNormal);
    state.sheenBitangent = cross(state.sheenNormal, state.sheenTangent);
    state.sheenLtc = PrincipledSheenLtc(saturate(dot(state.sheenNormal, state.view)), material.sheenRoughness);
    state.sheenColor = material.sheenWeight * material.sheenTint * state.sheenLtc.z;
    state.sheenTransmission = max(1.0 - PrincipledMaxChannel(state.sheenColor), 0.0);
    const float coatReflectance = integral.coat.singleScatter.r + state.coatMultipleScatter.r;
    const float coatTransmission = max(1.0 - material.coatWeight * coatReflectance, 0.0);
    state.lowerTransmission = state.sheenTransmission * coatTransmission
        * PrincipledCoatTransmission(material, state.view);
    return state;
}

float3 EvaluateLayeredLight(PrincipledLayeredState state, float3 toLight, float3 radiance)
{
    const PrincipledSurface material = state.material;
    const float3 light = PrincipledUnitNormal(toLight, material.normal);
    const float3 halfVector = PrincipledUnitNormal(state.view + light, material.normal);
    float3 base = float3(0.0);
    const float nl = saturate(dot(material.normal, light));
    if (nl > 0.0 && dot(material.normal, state.view) > 0.0)
    {
        const float distribution = PrincipledAnisotropicDistribution(halfVector, material.normal,
            state.tangent, state.bitangent, state.alpha);
        const float visibility = PrincipledAnisotropicVisibility(state.view, light, material.normal,
            state.tangent, state.bitangent, state.alpha);
        base = nl * (EvaluatePrincipledFresnel(state.reflection, dot(state.view, halfVector))
            * distribution * visibility + (state.baseMultipleScatter + state.baseDiffuse) * INV_PI);
    }
    float3 coat = float3(0.0);
#if (LX_MATERIAL_FEATURE_MASK & 0x0080u) != 0
    const float coatNl = saturate(dot(material.coatNormal, light));
    if (material.coatWeight > 0.0 && coatNl > 0.0 && dot(material.coatNormal, state.view) > 0.0)
    {
        const float coatNv = saturate(dot(material.coatNormal, state.view));
        const float coatAlpha = max(material.coatRoughness * material.coatRoughness, 1e-3);
        coat = material.coatWeight * coatNl * (PrincipledCoatFresnel(material,
            dot(state.view, halfVector)) * DistributionGGX(saturate(dot(material.coatNormal, halfVector)), coatAlpha)
            * VisibilitySmithHeightCorrelated(coatNv, coatNl, coatAlpha)
            + state.coatMultipleScatter * INV_PI);
    }
#endif
    float3 sheen = float3(0.0);
#if (LX_MATERIAL_FEATURE_MASK & 0x0100u) != 0
    const float sheenNl = saturate(dot(state.sheenNormal, light));
    if (sheenNl > 0.0 && dot(state.sheenNormal, state.view) > 0.0)
    {
        const float a = state.sheenLtc.x;
        const float b = state.sheenLtc.y;
        const float3 local = float3(dot(state.sheenTangent, light), dot(state.sheenBitangent, light), sheenNl);
        const float3 transformed = float3(a * local.x + b * local.z, a * local.y, local.z);
        const float lengthSquared = dot(transformed, transformed);
        const float density = sheenNl * INV_PI * a * a / max(lengthSquared * lengthSquared, 1e-20);
        sheen = state.sheenColor * density;
    }
#endif
    return (state.lowerTransmission * base + state.sheenTransmission * coat + sheen) * radiance;
}

struct PrincipledLayeredAmbient
{
    float3 diffuse;
    float3 specular;
    float3 coat;
    float3 sheen;
    float3 emission;

    float3 Sum()
    {
        return diffuse + specular + coat + sheen;
    }
};

PrincipledLayeredAmbient EvaluateLayeredIbl(PrincipledLayeredState state, float3 irradiance,
    float3 basePrefiltered, float3 coatPrefiltered, float3 sheenIrradiance, float3 coatIrradiance)
{
    const PrincipledSurface material = state.material;
    const float baseNv = saturate(dot(material.normal, state.view));
    const float coatNv = saturate(dot(material.coatNormal, state.view));
    const float baseAo = ComputeSpecularOcclusion(baseNv, material.occlusion, material.roughness);
    const float coatAo = ComputeSpecularOcclusion(coatNv, material.occlusion, material.coatRoughness);
    PrincipledLayeredAmbient ambient;
    ambient.diffuse = state.lowerTransmission * (state.baseDiffuse + state.baseMultipleScatter)
        * irradiance * material.occlusion;
    ambient.specular = state.lowerTransmission * state.integral.base.singleScatter * basePrefiltered * baseAo;
    ambient.coat = state.sheenTransmission * material.coatWeight
        * (state.integral.coat.singleScatter * coatPrefiltered * coatAo
            + state.coatMultipleScatter * coatIrradiance * material.occlusion);
    ambient.sheen = state.sheenColor * sheenIrradiance * material.occlusion;
    ambient.emission = state.lowerTransmission * material.emission;
    return ambient;
}

// Existing uniform-environment callers retain their convention. Production
// directional bake consumers supply the independently oriented coat response.
PrincipledLayeredAmbient EvaluateLayeredIbl(PrincipledLayeredState state, float3 irradiance,
    float3 basePrefiltered, float3 coatPrefiltered, float3 sheenIrradiance)
{
    return EvaluateLayeredIbl(state, irradiance, basePrefiltered, coatPrefiltered, sheenIrradiance, irradiance);
}
