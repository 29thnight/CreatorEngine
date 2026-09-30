#pragma once

float3 BakeRadiance(float3 direction)
{
    return SanitizeRadiance(gEnvironment.SampleLevel(gEnvironmentSampler, direction, 0).rgb);
}

float3 BakeDiffuse(float3 normal)
{
    // Use the Scene renderer's MIS-convolved irradiance when available. The
    // per-pixel cosine sequence misses small bright HDR emitters and darkens
    // graph materials relative to the same GBuffer shaded by Deferred.
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_precomputed != 0u)
        return SanitizeRadiance(gIrradiance.SampleLevel(gIblSampler, normal, 0).rgb);
#endif
    float3 sum = float3(0);
    for (uint i = 0; i < 1024u; ++i)
    {
        sum += BakeRadiance(SampleHemisphereCosine(Hammersley(i, 1024u), normal));
    }
    return sum / 1024.0;
}

// View-dependent Fresnel-weighted directional convolution. Its RGB denominator
// is essential for tinted dielectrics, F82 and thin film. A single isotropic
// cubemap mip cannot replace this response when the tangent/view changes.
float3 BakeReflection(PrincipledSurface material, float3 view, bool layered, bool coat)
{
    const float3 normal = coat ? material.coatNormal : material.normal;
    const float nv = saturate(dot(normal, view));
    if (nv <= 0.0)
    {
        return float3(0);
    }
    const float roughness = coat ? material.coatRoughness : material.roughness;
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_precomputed != 0u && !layered)
        return SanitizeRadiance(gPrefiltered.SampleLevel(gIblSampler,
            reflect(-view, normal), roughness * 5.0).rgb);
#endif
    if (roughness == 0.0)
    {
        return BakeRadiance(reflect(-view, normal));
    }
    const float3 tangent = PrincipledProjectedTangent(layered ? material.tangent : view, normal);
    const float3 bitangent = cross(normal, tangent);
    const float alphaScalar = max(roughness * roughness, 1e-3);
    const float2 alpha = coat ? float2(alphaScalar) : PrincipledAnisotropicAlpha(material);
    const PrincipledReflection reflection = BuildPrincipledReflection(material);
    float3 sum = float3(0);
    float3 denominator = float3(0);
    for (uint i = 0; i < 1024u; ++i)
    {
        float3 halfVector;
        if (layered)
        {
            halfVector = PrincipledSampleAnisotropic(Hammersley(i, 1024u), normal, tangent, bitangent, alpha);
        }
        else
        {
            const float3 local = ImportanceSampleGGX(Hammersley(i, 1024u), float3(0, 0, 1), roughness);
            halfVector = local.x * tangent + local.y * bitangent + local.z * normal;
        }
        const float vh = saturate(dot(view, halfVector));
        const float3 light = 2.0 * vh * halfVector - view;
        const float nl = saturate(dot(normal, light));
        if (nl <= 0.0)
        {
            continue;
        }
        const float weight =
            layered ? 4.0 * nl * PrincipledAnisotropicVisibility(view, light, normal, tangent, bitangent, alpha) * vh /
                          max(dot(normal, halfVector), 1e-6)
                    : GeometrySchlickGgxIbl(nv, roughness) * GeometrySchlickGgxIbl(nl, roughness) * vh /
                          max(dot(normal, halfVector) * nv, 1e-6);
        const float3 fresnel = coat ? PrincipledCoatFresnel(material, vh) : EvaluatePrincipledFresnel(reflection, vh);
        const float3 response = fresnel * weight;
        sum += response * BakeRadiance(light);
        denominator += response;
    }
    return select(denominator > 0.0, sum / max(denominator, 1e-20), float3(0));
}

float3 BakeSheen(PrincipledLayeredState state)
{
    if (state.material.sheenWeight <= 0.0 || dot(state.sheenNormal, state.view) <= 0.0)
    {
        return float3(0);
    }
    float3 sum = float3(0);
    for (uint i = 0; i < 1024u; ++i)
    {
        // Inverse LTC applied to a cosine sample. M preserves hemisphere z;
        // its Jacobian gives the same density as EvaluateLayeredLight.
        const float2 xi = Hammersley(i, 1024u);
        const float phi = 2.0 * PI * xi.x;
        const float3 cosine = float3(sqrt(xi.y) * cos(phi), sqrt(xi.y) * sin(phi), sqrt(1.0 - xi.y));
        const float3 local = normalize(
            float3((cosine.x - state.sheenLtc.y * cosine.z) / state.sheenLtc.x, cosine.y / state.sheenLtc.x, cosine.z));
        const float3 direction =
            local.x * state.sheenTangent + local.y * state.sheenBitangent + local.z * state.sheenNormal;
        sum += BakeRadiance(direction);
    }
    return sum / 1024.0;
}
