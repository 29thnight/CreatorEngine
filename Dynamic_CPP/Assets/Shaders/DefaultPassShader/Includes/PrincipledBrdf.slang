#pragma once

#include "PrincipledSurface.slang"
#include "Ibl.slang"
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
#include "PrincipledThinFilm.slang"
#endif

struct PrincipledReflection
{
    float ior;
    float metallic;
    float3 dielectricF0;
    float3 metalF0;
    float3 metalF82Correction;
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    float thinFilmThickness;
    float thinFilmIor;
    float3 metalIor;
    float3 metalExtinction;
#endif
};

PrincipledReflection BuildPrincipledReflection(PrincipledSurface material)
{
    PrincipledReflection reflection;
    reflection.ior = material.ior;
    reflection.metallic = material.metallic;
    reflection.dielectricF0 = material.dielectricF0;
    reflection.metalF0 = material.metalF0;
    reflection.metalF82Correction = material.metalF82Correction;
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    reflection.thinFilmThickness = material.thinFilmThickness;
    reflection.thinFilmIor = material.thinFilmIor;
    const float x82 = 6.0 / 7.0;
    const float x82Fifth = x82 * x82 * x82 * x82 * x82;
    const float3 f82 = saturate(lerp(material.metalF0, float3(1.0), x82Fifth)
        - material.metalF82Correction * (1.0 / 7.0) * x82Fifth * x82);
    FilmConductorIor(material.metalF0, f82, reflection.metalIor, reflection.metalExtinction);
#endif
    return reflection;
}

float PrincipledDielectricFresnel(float cosine, float ior)
{
    // Equal indices have no interface, including at grazing incidence.
    if (ior == 1.0)
    {
        return 0.0;
    }

    const float cosIncident = saturate(cosine);
    const float cosTransmittedSquared = 1.0
        - (1.0 - cosIncident * cosIncident) / (ior * ior);
    if (cosTransmittedSquared <= 0.0)
    {
        return 1.0;
    }

    const float cosTransmitted = sqrt(cosTransmittedSquared);
    const float s = (cosIncident - ior * cosTransmitted)
        / max(cosIncident + ior * cosTransmitted, 1e-20);
    const float p = (ior * cosIncident - cosTransmitted)
        / max(ior * cosIncident + cosTransmitted, 1e-20);
    return 0.5 * (s * s + p * p);
}

float3 EvaluatePrincipledFresnel(PrincipledReflection reflection, float cosine)
{
    const float cosineClamped = saturate(cosine);
    const float x = 1.0 - cosineClamped;
    const float xSquared = x * x;
    const float xFifth = xSquared * xSquared * x;
    float3 metal = saturate(lerp(reflection.metalF0, float3(1.0), xFifth)
        - reflection.metalF82Correction * cosineClamped * xFifth * x);

    float3 dielectric = float3(0.0);
    if (reflection.ior != 1.0)
    {
        const float f0Root = (reflection.ior - 1.0) / (reflection.ior + 1.0);
        const float physicalF0 = f0Root * f0Root;
        const float physicalF = PrincipledDielectricFresnel(cosineClamped, reflection.ior);
        const float interpolation = saturate((physicalF - physicalF0)
            / max(1.0 - physicalF0, 1e-20));
        dielectric = lerp(reflection.dielectricF0, float3(1.0), interpolation);
    }
#if (LX_MATERIAL_FEATURE_MASK & 0x0400u) != 0
    if (reflection.thinFilmThickness > 0.0 && reflection.thinFilmIor != 1.0)
    {
        const float transition = smoothstep(0.0, 1.0, reflection.thinFilmThickness);
        const float filmIor = lerp(1.0, reflection.thinFilmIor, transition);
        float3 filmDielectric = EvaluateFilmRgb(cosineClamped, filmIor,
            reflection.thinFilmThickness, float3(reflection.ior), float3(0.0));
        const float f0Root = (reflection.ior - 1.0) / (reflection.ior + 1.0);
        const float physicalF0 = f0Root * f0Root;
        if (physicalF0 > 1e-5)
        {
            const float3 interpolation = saturate((1.0 - filmDielectric) / (1.0 - physicalF0));
            filmDielectric *= lerp(float3(1.0), reflection.dielectricF0 / physicalF0, interpolation);
        }
        dielectric = lerp(dielectric, saturate(filmDielectric), transition);
        const float3 filmMetal = EvaluateFilmRgb(cosineClamped, filmIor, reflection.thinFilmThickness,
            reflection.metalIor, reflection.metalExtinction);
        metal = lerp(metal, filmMetal, transition);
    }
#endif
    return lerp(dielectric, metal, reflection.metallic);
}

// Baked response for an IOR-aware lookup. A legacy two-channel Schlick DFG
// cannot encode physical dielectric Fresnel and F82 Tint. Resource binding and
// cache/packing of this response belong to MAT-6/MAT-7.
struct PrincipledIblIntegral
{
    float3 singleScatter;
    float directionalAlbedo;
    float3 averageFresnel;
};

// Bake/probe helper, never called in the production pixel shader. Integrate the
// same Fresnel model used by direct lights instead of substituting F0=0.04.
PrincipledIblIntegral IntegratePrincipledIbl(PrincipledSurface material, float ndotv)
{
    const float cosine = clamp(ndotv, 1e-4, 1.0);
    const float3 view = float3(sqrt(1.0 - cosine * cosine), 0.0, cosine);
    const float3 normal = float3(0.0, 0.0, 1.0);
    const PrincipledReflection reflection = BuildPrincipledReflection(material);
    const float geometryView = GeometrySchlickGgxIbl(cosine, material.roughness);
    const uint sampleCount = 1024u;
    PrincipledIblIntegral integral;
    integral.singleScatter = float3(0.0);
    integral.directionalAlbedo = 0.0;
    integral.averageFresnel = float3(0.0);

    if (material.roughness == 0.0)
    {
        // Delta reflection has an analytic limit. NDF sampling at tiny NoV
        // amplifies float normalization error even when every H should be N.
        integral.singleScatter = EvaluatePrincipledFresnel(reflection, cosine);
        integral.directionalAlbedo = 1.0;
    }
    else
    {
        for (uint index = 0u; index < sampleCount; ++index)
        {
            const float3 halfVector = ImportanceSampleGGX(Hammersley(index, sampleCount),
                normal, material.roughness);
            const float vdoth = saturate(dot(view, halfVector));
            const float3 toLight = 2.0 * vdoth * halfVector - view;
            const float ndotl = saturate(toLight.z);
            if (ndotl > 0.0)
            {
                const float geometry = geometryView
                    * GeometrySchlickGgxIbl(ndotl, material.roughness);
                const float visibility = geometry * vdoth / max(halfVector.z * cosine, 1e-6);
                integral.singleScatter += EvaluatePrincipledFresnel(reflection, vdoth) * visibility;
                integral.directionalAlbedo += visibility;
            }
        }
        integral.singleScatter /= float(sampleCount);
        integral.directionalAlbedo = saturate(integral.directionalAlbedo / float(sampleCount));
    }

    const uint averageSamples = 64u;
    for (uint index = 0u; index < averageSamples; ++index)
    {
        // Cosine-weighted hemisphere mean; midpoint avoids a grazing singularity.
        const float cosineSample = sqrt((float(index) + 0.5) / float(averageSamples));
        integral.averageFresnel += EvaluatePrincipledFresnel(reflection, cosineSample);
    }
    integral.averageFresnel /= float(averageSamples);
    return integral;
}

IblAmbient EvaluatePrincipledIbl(PrincipledSurface material, float ndotv,
    float3 irradiance, float3 prefiltered, PrincipledIblIntegral integral)
{
    const float missingEnergy = saturate(1.0 - integral.directionalAlbedo);
    const float3 multipleScatter = missingEnergy * integral.singleScatter
        * integral.averageFresnel / max(1.0 - integral.averageFresnel * missingEnergy, 1e-4);
    const float3 diffuseWeight = max(1.0 - integral.singleScatter - multipleScatter, 0.0);
    const float3 diffuse = material.baseColor * (1.0 - material.metallic) * diffuseWeight;

    IblAmbient result;
    result.diffuse = (diffuse + multipleScatter) * irradiance * material.occlusion;
    result.specular = integral.singleScatter * prefiltered
        * ComputeSpecularOcclusion(ndotv, material.occlusion, material.roughness);
    return result;
}
