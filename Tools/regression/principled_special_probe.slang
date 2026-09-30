#ifndef LX_MATERIAL_FEATURE_MASK
#define LX_MATERIAL_FEATURE_MASK 0x387Fu
#endif

#include "../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialForwardSurface.slang"

struct BaseInput
{
    float4 baseAlpha;
    float4 normalRoughness;
    float4 metalIorLevelAo;
    float4 tintAnisotropy;
    float4 emissionColorStrength;
    float4 coatWeightRoughIorFilmThickness;
    float4 coatTintFilmIor;
    float4 coatNormalSheenWeight;
    float4 sheenTintRoughness;
    float4 tangentRotation;
    float4 options;
};

struct ProbeInput
{
    BaseInput base;
    float4 specialWeightScaleIor;
    float4 radiusAnisotropy;
    float4 volumeColorDensity;
    float4 volumeAbsorptionG;
    float4 volumeEmissionStrength;
    float4 distanceFaceSource;
    float4 profileDistance;
};

StructuredBuffer<ProbeInput> Inputs : register(t0);
RWStructuredBuffer<float4> Results : register(u0);

// The fragment entry proves that the explicit Forward consumer accepts prepared
// transport inputs without invoking the bake integrators in the pixel shader.
StructuredBuffer<SpecialForwardIntegral> ForwardLookup : register(t1);
StructuredBuffer<SpecialForwardEnvironment> ForwardEnvironment : register(t2);
StructuredBuffer<VolumeSegment> ForwardMedium : register(t3);

PrincipledSurface ProbeMaterial(ProbeInput input)
{
    MaterialInputs material = DefaultMaterialInputs();
    if (input.base.options.x == 0.0)
    {
        material.baseColor = input.base.baseAlpha.rgb;
        material.alpha = input.base.baseAlpha.a;
        material.normal = input.base.normalRoughness.xyz;
        material.roughness = input.base.normalRoughness.w;
        material.metallic = input.base.metalIorLevelAo.x;
        material.ior = input.base.metalIorLevelAo.y;
        material.specularIorLevel = input.base.metalIorLevelAo.z;
        material.occlusion = input.base.metalIorLevelAo.w;
        material.specularTint = input.base.tintAnisotropy.rgb;
        material.anisotropy = input.base.tintAnisotropy.w;
        material.emissionColor = input.base.emissionColorStrength.rgb;
        material.emissionStrength = input.base.emissionColorStrength.w;
        material.coatWeight = input.base.coatWeightRoughIorFilmThickness.x;
        material.coatRoughness = input.base.coatWeightRoughIorFilmThickness.y;
        material.coatIor = input.base.coatWeightRoughIorFilmThickness.z;
        material.thinFilmThickness = input.base.coatWeightRoughIorFilmThickness.w;
        material.coatTint = input.base.coatTintFilmIor.rgb;
        material.thinFilmIor = input.base.coatTintFilmIor.w;
        material.coatNormal = input.base.coatNormalSheenWeight.xyz;
        material.sheenWeight = input.base.coatNormalSheenWeight.w;
        material.sheenTint = input.base.sheenTintRoughness.rgb;
        material.sheenRoughness = input.base.sheenTintRoughness.w;
        material.tangent = input.base.tangentRotation.xyz;
        material.anisotropyRotation = input.base.tangentRotation.w;
        material.transmissionWeight = input.specialWeightScaleIor.x;
        material.subsurfaceWeight = input.specialWeightScaleIor.y;
        material.subsurfaceScale = input.specialWeightScaleIor.z;
        material.subsurfaceIor = input.specialWeightScaleIor.w;
        material.subsurfaceRadius = input.radiusAnisotropy.xyz;
        material.subsurfaceAnisotropy = input.radiusAnisotropy.w;
    }
    return OrientSpecialSurface(EvaluateMaterial(material), input.distanceFaceSource.y > 0.0);
}

PrincipledVolume ProbeVolume(ProbeInput input)
{
    VolumeInputs volume = DefaultVolumeInputs();
    if (input.base.options.x == 0.0)
    {
        volume.color = input.volumeColorDensity.rgb;
        volume.density = input.volumeColorDensity.w;
        volume.absorptionColor = input.volumeAbsorptionG.rgb;
        volume.anisotropy = input.volumeAbsorptionG.w;
        volume.emissionColor = input.volumeEmissionStrength.rgb;
        volume.emissionStrength = input.volumeEmissionStrength.w;
    }
    return EvaluateVolume(volume);
}

SpecialForwardEnvironment WhiteEnvironment()
{
    SpecialForwardEnvironment environment;
    environment.irradiance = float3(1.0);
    environment.basePrefiltered = float3(1.0);
    environment.glassPrefiltered = float3(1.0);
    environment.coatPrefiltered = float3(1.0);
    environment.sheenIrradiance = float3(1.0);
    environment.refractedRadiance = float3(1.0);
    environment.subsurfaceIrradiance = float3(1.0);
    return environment;
}

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const ProbeInput input = Inputs[id.x];
    const PrincipledSurface material = ProbeMaterial(input);
    const float cosines[3] = {0.1, 0.5, 0.9};
    const float cosine = cosines[id.y % 3u];
    const float sine = sqrt(1.0 - cosine * cosine);
    const float sign = input.distanceFaceSource.y > 0.0 ? 1.0 : -1.0;
    const float3 view = id.y < 3u ? float3(sine, 0.0, sign * cosine) : float3(0.0, sine, sign * cosine);
    const float3 light = normalize(float3(0.4, -0.1, sign * 0.9));
    const float3 backLight = light - 2.0 * dot(material.normal, light) * material.normal;
    const SpecialForwardIntegral integral = IntegrateSpecialForward(material, view);
    const SpecialForwardSurface surface = BuildSpecialForwardSurface(material, view, integral);
    const SpecialForwardLight direct = EvaluateSpecialLight(surface, light, float3(1.0));
    const SpecialForwardLight back = EvaluateSpecialLight(surface, backLight, float3(1.0));
    const SpecialForwardAmbient ambient = EvaluateSpecialIbl(surface, WhiteEnvironment());
    const PrincipledVolume volume = ProbeVolume(input);
    const float distance = input.distanceFaceSource.x;
    const float3 incident = float3(input.distanceFaceSource.w);
    const VolumeSegment segment = IntegrateVolumeSegment(volume, distance, incident);
    const VolumeSegment half = IntegrateVolumeSegment(volume, max(distance, 0.0) * 0.5, incident);
    const VolumeSegment split = ComposeVolumeSegments(half, half);
    const float3 ray = refract(-view, material.normal, rcp(surface.glass.ior));
    const uint offset = (id.x * 6u + id.y) * 32u;
    Results[offset + 0u] = float4(material.transmissionWeight, material.subsurfaceWeight,
        material.subsurfaceScale, material.subsurfaceIor);
    Results[offset + 1u] = float4(material.subsurfaceRadius, material.subsurfaceAnisotropy);
    Results[offset + 2u] = float4(material.transmissionF0, material.transmissionIor);
    Results[offset + 3u] = float4(material.normal, material.ior);
    Results[offset + 4u] = float4(integral.glass.singleScatter, integral.glass.directionalAlbedo);
    Results[offset + 5u] = float4(integral.glass.averageFresnel, 0.0);
    Results[offset + 6u] = float4(integral.rawTransmission, 0.0);
    Results[offset + 7u] = float4(surface.transmission, surface.glassWeight);
    Results[offset + 8u] = float4(surface.diffuse, 0.0);
    Results[offset + 9u] = float4(surface.subsurface, 0.0);
    Results[offset + 10u] = float4(direct.local, 0.0);
    Results[offset + 11u] = float4(direct.subsurfaceSource, 0.0);
    Results[offset + 12u] = float4(back.transmission, 0.0);
    Results[offset + 13u] = float4(ambient.local, 0.0);
    Results[offset + 14u] = float4(ambient.subsurface, 0.0);
    Results[offset + 15u] = float4(ambient.transmission, 0.0);
    Results[offset + 16u] = float4(ambient.Sum(), 0.0);
    Results[offset + 17u] = float4(ambient.emission, material.alpha);
    Results[offset + 18u] = float4(EvaluateSubsurfaceProfile(surface.profile, input.profileDistance.x), 0.0);
    Results[offset + 19u] = float4(SubsurfaceProfileCdf(surface.profile, input.profileDistance.x), 0.0);
    Results[offset + 20u] = float4(float3(surface.profile.local), volume.anisotropy);
    Results[offset + 21u] = float4(volume.scattering, 0.0);
    Results[offset + 22u] = float4(volume.absorption, 0.0);
    Results[offset + 23u] = float4(volume.emission, 0.0);
    Results[offset + 24u] = float4(segment.transmittance, 0.0);
    Results[offset + 25u] = float4(segment.radiance, 0.0);
    Results[offset + 26u] = float4(CompositeSpecialForward(ambient.Sum() + ambient.emission, material.alpha,
        float3(0.2, 0.4, 0.6), segment), 0.0);
    Results[offset + 27u] = float4(split.radiance - segment.radiance,
        length(split.transmittance - segment.transmittance));
    Results[offset + 28u] = float4(ray, dot(ray, ray) > 0.0 ? 1.0 : 0.0);
    Results[offset + 29u] = float4(EvaluatePrincipledFresnel(surface.glassReflection, cosine), 0.0);
    Results[offset + 30u] = float4(EvaluateSpecialLight(surface, light, float3(2.0)).local, 0.0);
    Results[offset + 31u] = float4(VolumePhase(input.distanceFaceSource.z, volume.anisotropy),
        VolumePhase(-input.distanceFaceSource.z, volume.anisotropy), VolumePhase(0.0, 0.0), 0.0);
}

float4 PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    const PrincipledSurface material = ProbeMaterial(Inputs[0]);
    const float3 view = normalize(float3(0.6, 0.0, 0.8));
    const SpecialForwardSurface surface = BuildSpecialForwardSurface(material, view, ForwardLookup[0]);
    const SpecialForwardAmbient ambient = EvaluateSpecialIbl(surface, ForwardEnvironment[0]);
    const float3 color = CompositeSpecialForward(ambient.Sum() + ambient.emission, material.alpha,
        float3(0.2, 0.4, 0.6), ForwardMedium[0]);
    return float4(color, 1.0);
}
