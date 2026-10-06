#pragma once

LXSceneSubsurfaceReflection LXSceneRuntimeReflection(PrincipledIblBakePoint point)
{
    LXSceneSubsurfaceReflection sample = (LXSceneSubsurfaceReflection)0;
    const PrincipledSurface material = EvaluateIblBakePoint(point);
    const float3 view = normalize(point.viewTier.xyz);
    const PrincipledSurface metal = SpecialMetal(material);
    const PrincipledSurface dielectric = SpecialDielectric(material);
    const PrincipledIblIntegral m = IntegrateLayeredReflection(metal, view, false);
    const PrincipledIblIntegral d = IntegrateLayeredReflection(dielectric, view, false);
    sample.metalSingleAlbedo = float4(m.singleScatter, m.directionalAlbedo);
    sample.metalAverage = float4(m.averageFresnel, 1);
    sample.dielectricSingleAlbedo = float4(d.singleScatter, d.directionalAlbedo);
    sample.dielectricAverage = float4(d.averageFresnel, 0);
    if (lx_hasEnvironment != 0u)
    {
        sample.metalEnvironment = float4(BakeReflection(metal, view, true, false), 0);
        sample.dielectricEnvironment = float4(BakeReflection(dielectric, view, true, false), 0);
    }
    return sample;
}

LXSceneRefractionSample LXSceneRuntimeRefraction(PrincipledIblBakePoint point, PrincipledSurface evaluated,
    float3 position)
{
    LXSceneRefractionSample sample = (LXSceneRefractionSample)0;
#if (LX_MATERIAL_FEATURE_MASK & 0x0800u) != 0
    PrincipledSurface material = EvaluateIblBakePoint(point);
    material.normal = evaluated.normal;
    material.transmissionIor = evaluated.transmissionIor;
    material.transmissionFilmIor = evaluated.transmissionFilmIor;
    material.transmissionF0 = saturate(pow((material.transmissionIor - 1) / (material.transmissionIor + 1), 2)
        * max(point.tintAnisotropy.rgb, 0));
    const PrincipledSurface glass = SpecialGlass(material);
    const float3 view = normalize(point.viewTier.xyz);
    const PrincipledIblIntegral integral = IntegrateLayeredReflection(glass, view, false, false);
    sample.glassSingleAlbedo = float4(integral.singleScatter, integral.directionalAlbedo);
    sample.glassAverage = float4(integral.averageFresnel, 1);
    sample.rawTransmission = float4(IntegrateSpecialTransmission(glass, view), 0);
    if (lx_hasEnvironment != 0u)
    {
        sample.glassEnvironment = float4(BakeReflection(glass, view, true, false), 0);
    }
    float hitFraction;
    sample.radiance.rgb = LXRefractionTransport(glass, view, position, hitFraction);
    sample.radiance.w = hitFraction;
#endif
    return sample;
}

LXSceneRuntimeEffectSample LXSceneCaptureRuntimeEffects(LXSceneVertex input, bool frontFace)
{
    input.clip.xy += float2(lx_effectSourceX, lx_effectSourceY);
    input = LXScenePixelCenter(input);
    LXSceneRuntimeEffectSample output = (LXSceneRuntimeEffectSample)0;
    const float area = length(cross(ddx_fine(input.position), ddy_fine(input.position)));
    const PrincipledSurface material = LXSceneMaterial(input, frontFace);
    const PrincipledIblBakePoint point = LXScenePoint(input, frontFace);
    if (!IsValidScenePrincipledIblBakePoint(point))
    {
        return output;
    }
    if (lx_blended == 1u && material.alpha <= 0.0)
    {
        discard;
    }
    const uint2 pixel = uint2(input.clip.xy);
    const uint owner = lx_owners.Load(int3(pixel, 0));
    if (owner != lx_drawOwner)
    {
        discard;
    }
    const uint2 local = pixel - uint2(lx_effectSourceX, lx_effectSourceY);
    if (local.x >= lx_effectSourceWidth || local.y >= lx_effectSourceHeight)
    {
        discard;
    }
    output.owner = owner;
    output.reflection = LXSceneRuntimeReflection(point);
    output.refraction = LXSceneRuntimeRefraction(point, material, input.position);
    if (material.subsurfaceWeight > 0.0)
    {
        const float3 view = PrincipledUnitNormal(lx_eye.xyz - input.position, material.normal);
        const PrincipledIblBakeSample sample = LXSplitSumSample(EvaluateIblBakePoint(point),
            normalize(point.viewTier.xyz), point.viewTier.w == 1.0);
        const SpecialForwardSurface surface = LXSceneSpecialSurfaceFromSamples(material, view, sample,
            output.reflection, output.refraction);
        float3 irradiance;
        LXSceneSpecialDirect(surface, input.clip.xy, input.position, irradiance);
        if (lx_hasEnvironment != 0u)
        {
            irradiance += sample.irradiance.rgb;
        }
        output.sourceArea = float4(irradiance, area);
        output.positionValid = float4(input.position, 1);
        output.normal = material.normal;
    }
    return output;
}

float3 LXSceneRuntimeSubsurface(PrincipledSurface material, uint index)
{
    const uint2 pixel = uint2(index % lx_width, index / lx_width);
    const LXSceneRuntimeEffectSample center = LXSceneLoadRuntimeSource(index);
    if (center.positionValid.w != 1.0)
    {
        return float3(0);
    }
    // 원래 gather의 반경 4와 순서/float32 연산을 유지한다. 목적지 profile은
    // material 레지스터에서 만들므로 4장의 전체 화면 profile MRT가 필요 없다.
    const PrincipledSubsurfaceProfile profile = BuildSceneSubsurfaceProfile(material);
    const float3 source = center.sourceArea.rgb;
    float3 sum = float3(0);
    float3 denominator = float3(0);
    for (int y = -4; y <= 4; ++y)
    {
        for (int x = -4; x <= 4; ++x)
        {
            const int2 neighbor = int2(pixel) + int2(x, y);
            if (any(neighbor < 0) || neighbor.x >= int(lx_width) || neighbor.y >= int(lx_height))
            {
                continue;
            }
            const int2 local = neighbor - int2(lx_effectSourceX, lx_effectSourceY);
            if (any(local < 0) || local.x >= int(lx_effectSourceWidth) || local.y >= int(lx_effectSourceHeight))
            {
                continue;
            }
            const LXSceneRuntimeEffectSample other = LXSceneLoadRuntimeSource(uint(neighbor.y) * lx_width + uint(neighbor.x));
            if (other.positionValid.w != 1.0 || other.owner != center.owner || dot(center.normal, other.normal) < 0.5)
            {
                continue;
            }
            const float3 weight = EvaluateSubsurfaceProfile(profile,
                length(center.positionValid.xyz - other.positionValid.xyz)) * max(other.sourceArea.w, 1e-12);
            sum += weight * other.sourceArea.rgb;
            denominator += weight;
        }
    }
    return select(profile.local || denominator <= 0.0, source, sum / max(denominator, 1e-20));
}

struct LXSceneRuntimeEffects0
{
    float4 fields[8] : SV_Target;
};
struct LXSceneRuntimeEffects1
{
    float4 fields[6] : SV_Target;
};

[shader("fragment")]
LXSceneRuntimeEffects0 LXSceneRuntimeEffects0PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    const LXSceneRuntimeEffectSample sample = LXSceneCaptureRuntimeEffects(input, frontFace);
    LXSceneRuntimeEffects0 output;
    output.fields[0] = sample.sourceArea;
    output.fields[1] = sample.positionValid;
    output.fields[2] = float4(sample.normal, 0);
    output.fields[3] = sample.reflection.metalSingleAlbedo;
    output.fields[4] = sample.reflection.metalAverage;
    output.fields[5] = sample.reflection.dielectricSingleAlbedo;
    output.fields[6] = sample.reflection.dielectricAverage;
    output.fields[7] = sample.reflection.metalEnvironment;
    return output;
}

[shader("fragment")]
LXSceneRuntimeEffects1 LXSceneRuntimeEffects1PS(LXSceneVertex input, bool frontFace : SV_IsFrontFace)
{
    const LXSceneRuntimeEffectSample sample = LXSceneCaptureRuntimeEffects(input, frontFace);
    LXSceneRuntimeEffects1 output;
    output.fields[0] = sample.reflection.dielectricEnvironment;
    output.fields[1] = sample.refraction.glassSingleAlbedo;
    output.fields[2] = sample.refraction.glassAverage;
    output.fields[3] = sample.refraction.glassEnvironment;
    output.fields[4] = sample.refraction.rawTransmission;
    output.fields[5] = sample.refraction.radiance;
    return output;
}
