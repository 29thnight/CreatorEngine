#pragma once

#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
#include "IblImportance.slang"
float BakeEnvironmentPdf(float3 direction)
{
    uint size, rows;
    gImportanceRows.GetDimensions(size, rows);
    const float total=gImportanceMarginal.Load(int3(0,rows-1u,0)).r;
    float pdf;
    IblLookupCell(direction,size,total,pdf);
    return pdf;
}
#endif

#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
float3 BakeSourceRadiance(float3 direction)
{
    const float2 uv=float2(atan2(direction.z,direction.x)/(2.0*PI)+0.5,
        -asin(clamp(direction.y,-1.0,1.0))/PI+0.5);
    uint width,height; gEnvironmentSource.GetDimensions(width,height);
    const float2 pixel=uv*float2(width,height)-0.5;
    const int2 cell=int2(floor(pixel)); const float2 weight=frac(pixel);
    // Float lerp avoids the fixed subtexel filter precision, which is
    // visible in sharp high-contrast HDR emitters. Wrap longitude only.
    const int x0=(cell.x+int(width))%int(width), x1=(x0+1)%int(width);
    const int y0=clamp(cell.y,0,int(height)-1), y1=clamp(cell.y+1,0,int(height)-1);
    return SanitizeEnvironmentRadiance(lerp(
        lerp(gEnvironmentSource.Load(int3(x0,y0,0)).rgb,gEnvironmentSource.Load(int3(x1,y0,0)).rgb,weight.x),
        lerp(gEnvironmentSource.Load(int3(x0,y1,0)).rgb,gEnvironmentSource.Load(int3(x1,y1,0)).rgb,weight.x),weight.y));
}

#endif

float3 BakeRadiance(float3 direction)
{
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_source != 0u) return BakeSourceRadiance(direction);
    // s0 is the point sampler for material field textures in SceneLookup.
    // The directional environment is radiance and requires the linear IBL sampler.
    return SanitizeEnvironmentRadiance(gEnvironment.SampleLevel(gIblSampler, direction, 0).rgb);
#else
    return SanitizeEnvironmentRadiance(gEnvironment.SampleLevel(gEnvironmentSampler, direction, 0).rgb);
#endif
}

float3 BakeDiffuse(float3 normal)
{
    // Use the Scene renderer's MIS-convolved irradiance when available. The
    // per-pixel cosine sequence misses small bright HDR emitters and darkens
    // graph materials relative to the same GBuffer shaded by Deferred.
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_precomputed != 0u)
        return SanitizeEnvironmentRadiance(gIrradiance.SampleLevel(gIblSampler, normal, 0).rgb);
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
    if (roughness == 0.0)
    {
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
        if (lx_source != 0u)
        {
            return BakeSourceRadiance(reflect(-view,normal));
        }
#endif
        return BakeRadiance(reflect(-view, normal));
    }
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_precomputed != 0u && !layered)
        return SanitizeEnvironmentRadiance(gPrefiltered.SampleLevel(gIblSampler,
            reflect(-view, normal), roughness * 5.0).rgb);
#endif
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    uint proposalWidth,proposalHeight;
    gImportanceSamples.GetDimensions(proposalWidth,proposalHeight);
    const uint environmentSamples=proposalWidth==5120u ? 4096u : 1024u;
    const uint environmentOffset=proposalWidth==5120u ? 1024u : 0u;
    const float environmentRatio=float(environmentSamples)/1024.0;
#endif
    const float3 tangent = PrincipledProjectedTangent(layered ? material.tangent : view, normal);
    const float3 bitangent = cross(normal, tangent);
    const float alphaScalar = max(roughness * roughness, 1e-3);
    const float2 alpha = coat ? float2(alphaScalar) : PrincipledAnisotropicAlpha(material);
    const PrincipledReflection reflection = BuildPrincipledReflection(material);
    const PrincipledLayeredEnergy energy = BuildLayeredEnergy(material, view);
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
        const float3 fresnel = layered
            ? (coat ? PrincipledCoatFresnel(material, vh) * energy.coat : EvaluateLayeredFresnel(reflection, energy, vh))
            : EvaluatePrincipledFresnel(reflection, vh);
        float3 response = fresnel * weight;
        // The prepared directional albedo uses this same BRDF sequence.
        // Keep its denominator so consumption cancels that normalization;
        // an environment-dependent denominator would bias the lighting.
        denominator += response;
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
        if (lx_importance != 0u)
        {
            const float d=PrincipledAnisotropicDistribution(halfVector,normal,tangent,bitangent,
                layered ? alpha : float2(alphaScalar));
            const float pdf=d * saturate(dot(normal,halfVector)) / max(4.0*vh,1e-20);
            // Balance heuristic accounts for the environment/BRDF count ratio.
            // The CDF proposes directions; decoded source remains the radiance.
            response *= pdf / max(pdf+environmentRatio*BakeEnvironmentPdf(light),1e-20);
        }
#endif
        sum += response * BakeRadiance(light);
    }
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if (lx_importance != 0u)
        for (uint i=0u;i<environmentSamples;++i)
        {
            const float4 sample=gImportanceSamples.Load(int3(i+environmentOffset,0,0));
            const float3 light=sample.xyz;
            const float nl=saturate(dot(normal,light));
            if(nl<=0.0) continue;
            const float3 h=normalize(view+light);
            const float nh=saturate(dot(normal,h)), vh=saturate(dot(view,h));
            const float d=PrincipledAnisotropicDistribution(h,normal,tangent,bitangent,
                layered ? alpha : float2(alphaScalar));
            const float pdf=d*nh/max(4.0*vh,1e-20);
            const float kernel=layered
                ? d*nl*PrincipledAnisotropicVisibility(view,light,normal,tangent,bitangent,alpha)
                : d*GeometrySchlickGgxIbl(nv,roughness)*GeometrySchlickGgxIbl(nl,roughness)/max(4.0*nv,1e-20);
            const float3 fresnel=layered
                ? (coat ? PrincipledCoatFresnel(material,vh)*energy.coat : EvaluateLayeredFresnel(reflection,energy,vh))
                : EvaluatePrincipledFresnel(reflection,vh);
            const float3 response=fresnel*kernel/max(pdf+environmentRatio*sample.w,1e-20);
            sum+=response*BakeRadiance(light);
        }
#endif
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
        float weight=1.0;
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
        if(lx_importance!=0u)
        {
            const float3 transformed=float3(state.sheenLtc.x*local.x+state.sheenLtc.y*local.z,
                state.sheenLtc.x*local.y,local.z);
            const float lengthSquared=dot(transformed,transformed);
            const float pdf=INV_PI*max(local.z,0.0)*state.sheenLtc.x*state.sheenLtc.x/
                max(lengthSquared*lengthSquared,1e-20);
            weight=pdf/max(pdf+BakeEnvironmentPdf(direction),1e-20);
        }
#endif
        sum += weight*BakeRadiance(direction);
    }
#if defined(LX_SCENE_LOOKUP_PRECOMPUTED)
    if(lx_importance!=0u)
        for(uint i=0u;i<1024u;++i)
        {
            const float4 sample=gImportanceSamples.Load(int3(i,0,0));
            const float3 local=float3(dot(sample.xyz,state.sheenTangent),dot(sample.xyz,state.sheenBitangent),
                dot(sample.xyz,state.sheenNormal));
            const float3 transformed=float3(state.sheenLtc.x*local.x+state.sheenLtc.y*local.z,
                state.sheenLtc.x*local.y,local.z);
            const float lengthSquared=dot(transformed,transformed);
            const float pdf=INV_PI*max(local.z,0.0)*state.sheenLtc.x*state.sheenLtc.x/
                max(lengthSquared*lengthSquared,1e-20);
            const float weight=pdf/max(pdf+sample.w,1e-20);
            sum+=weight*BakeRadiance(sample.xyz);
        }
#endif
    return sum / 1024.0;
}
