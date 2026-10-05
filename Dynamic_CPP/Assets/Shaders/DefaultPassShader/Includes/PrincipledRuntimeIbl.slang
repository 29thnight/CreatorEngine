#pragma once

// 같은 split-sum 식을 bake와 runtime에서 공유한다. 픽셀별 float32 값은
// 레지스터에 유지하며 런타임용 전체 화면 bake 레코드를 만들지 않는다.
// Karis, "Real Shading in Unreal Engine 4" mobile EnvBRDFApprox: (A, B) of F0 * A + F90 * B.
float2 LXEnvironmentBrdfApprox(float roughness, float nv)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

PrincipledIblIntegral LXSplitSumIntegral(float roughness, float nv, float3 f0, float3 f90)
{
    PrincipledIblIntegral integral = PrincipledEmptyIntegral();
    if (nv <= 0.0)
    {
        return integral;
    }
    const float2 ab = LXEnvironmentBrdfApprox(roughness, nv);
    integral.singleScatter = f0 * ab.x + f90 * ab.y;
    integral.directionalAlbedo = saturate(ab.x + ab.y);
    // Schlick cosine-weighted hemisphere mean: F0 + (F90 - F0) / 21.
    integral.averageFresnel = f0 + (f90 - f0) / 21.0;
    return integral;
}

float3 LXSplitSumIrradiance(float3 normal)
{
    if (LX_SPLIT_SUM_PRECOMPUTED != 0u)
    {
        return SanitizeEnvironmentRadiance(LX_SPLIT_SUM_IRRADIANCE.SampleLevel(LX_SPLIT_SUM_SAMPLER, normal, 0).rgb);
    }
    return LX_SPLIT_SUM_RADIANCE(normal);
}

float3 LXSplitSumPrefiltered(float3 normal, float3 view, float roughness)
{
    const float3 direction = reflect(-view, normal);
    if (LX_SPLIT_SUM_PRECOMPUTED != 0u)
    {
        return SanitizeEnvironmentRadiance(LX_SPLIT_SUM_PREFILTERED.SampleLevel(LX_SPLIT_SUM_SAMPLER, direction, roughness * 5.0).rgb);
    }
    return LX_SPLIT_SUM_RADIANCE(direction);
}

// Anisotropy, angular Fresnel detail (F82, thin film) and the sheen lobe shape
// are reduced to the isotropic split-sum terms; sheen takes the base irradiance.
PrincipledIblBakeSample LXSplitSumSample(PrincipledSurface material, float3 view, bool layered)
{
    PrincipledIblBakeSample sample = (PrincipledIblBakeSample)0;
    const PrincipledReflection reflection = BuildPrincipledReflection(material);
    const PrincipledLayeredEnergy energy = BuildLayeredEnergy(material, view);
    const float baseNv = saturate(dot(material.normal, view));
    const float3 baseF0 = layered ? EvaluateLayeredFresnel(reflection, energy, 1.0)
                                  : EvaluatePrincipledFresnel(reflection, 1.0);
    const float3 baseF90 = layered ? EvaluateLayeredFresnel(reflection, energy, 0.0)
                                   : EvaluatePrincipledFresnel(reflection, 0.0);
    const PrincipledIblIntegral base = LXSplitSumIntegral(material.roughness, baseNv, baseF0, baseF90);
    const bool coat = layered && material.coatWeight > 0.0;
    PrincipledIblIntegral coatIntegral = PrincipledEmptyIntegral();
    if (coat)
    {
        coatIntegral = LXSplitSumIntegral(material.coatRoughness, saturate(dot(material.coatNormal, view)),
            PrincipledCoatFresnel(material, 1.0) * energy.coat, PrincipledCoatFresnel(material, 0.0) * energy.coat);
    }
    sample.baseSingleAlbedo = float4(base.singleScatter, base.directionalAlbedo);
    sample.baseAverage = float4(base.averageFresnel, 1.0);
    sample.coatSingleAlbedo = float4(coatIntegral.singleScatter, coatIntegral.directionalAlbedo);
    sample.coatAverage = float4(coatIntegral.averageFresnel, 0);
    if (LX_SPLIT_SUM_ENVIRONMENT != 0u)
    {
        const float3 irradiance = LXSplitSumIrradiance(material.normal);
        sample.irradiance = float4(irradiance, 0);
        sample.basePrefiltered = float4(any(base.singleScatter > 0.0)
            ? LXSplitSumPrefiltered(material.normal, view, material.roughness) : float3(0), 0);
        sample.coatPrefiltered = float4(coat && any(coatIntegral.singleScatter > 0.0)
            ? LXSplitSumPrefiltered(material.coatNormal, view, material.coatRoughness) : float3(0), 0);
        sample.coatIrradiance = float4(coat ? LXSplitSumIrradiance(material.coatNormal) : float3(0), 0);
        sample.sheenIrradiance = float4(layered && material.sheenWeight > 0.0 ? irradiance : float3(0), 0);
    }
    return sample;
}

