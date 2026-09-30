#ifndef LX_MATERIAL_FEATURE_MASK
#define LX_MATERIAL_FEATURE_MASK 0x07FFu
#endif

#include "../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PbrSurface.slang"
#include "../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang"

struct ProbeInput
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

StructuredBuffer<ProbeInput> Inputs : register(t0);
RWStructuredBuffer<float4> Results : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatch : SV_DispatchThreadID)
{
    const ProbeInput input = Inputs[dispatch.x];
    MaterialInputs authored = DefaultMaterialInputs();
    if (input.options.x == 0.0)
    {
        authored.baseColor = input.baseAlpha.rgb;
        authored.alpha = input.baseAlpha.a;
        authored.normal = input.normalRoughness.xyz;
        authored.roughness = input.normalRoughness.w;
        authored.metallic = input.metalIorLevelAo.x;
        authored.ior = input.metalIorLevelAo.y;
        authored.specularIorLevel = input.metalIorLevelAo.z;
        authored.occlusion = input.metalIorLevelAo.w;
        authored.specularTint = input.tintAnisotropy.rgb;
        authored.anisotropy = input.tintAnisotropy.w;
        authored.emissionColor = input.emissionColorStrength.rgb;
        authored.emissionStrength = input.emissionColorStrength.w;
        authored.coatWeight = input.coatWeightRoughIorFilmThickness.x;
        authored.coatRoughness = input.coatWeightRoughIorFilmThickness.y;
        authored.coatIor = input.coatWeightRoughIorFilmThickness.z;
        authored.thinFilmThickness = input.coatWeightRoughIorFilmThickness.w;
        authored.coatTint = input.coatTintFilmIor.rgb;
        authored.thinFilmIor = input.coatTintFilmIor.w;
        authored.coatNormal = input.coatNormalSheenWeight.xyz;
        authored.sheenWeight = input.coatNormalSheenWeight.w;
        authored.sheenTint = input.sheenTintRoughness.rgb;
        authored.sheenRoughness = input.sheenTintRoughness.w;
        authored.tangent = input.tangentRotation.xyz;
        authored.anisotropyRotation = input.tangentRotation.w;
    }
    const PrincipledSurface material = EvaluateMaterial(authored);
    const float cosines[4] = {0.1, 0.25, 0.5, 0.9};
    const float cosine = cosines[dispatch.y % 4u];
    const float sine = sqrt(1.0 - cosine * cosine);
    const float3 view = dispatch.y < 4u ? float3(sine, 0.0, cosine) : float3(0.0, sine, cosine);
    const float3 light = normalize(float3(0.35, -0.2, 0.8));
    const PrincipledLayeredIntegral integral = IntegrateLayeredIbl(material, view);
    const PrincipledLayeredState state = BuildLayeredState(material, view, integral);
    float3 direct;
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
    const PbrSurface surface = BuildPbrSurface(float3(0.0), view, material, PbrNeutralDfg(), integral);
    direct = surface.EvaluateLight(light, float3(1.0));
#else
    direct = EvaluateLayeredLight(state, light, float3(1.0));
#endif
    const PrincipledLayeredAmbient ambient = EvaluateLayeredIbl(state, float3(1.0),
        float3(1.0), float3(1.0), float3(1.0));
    const float3 halfVector = normalize(view + light);
    const uint offset = (dispatch.x * 8u + dispatch.y) * 26u;
    Results[offset + 0u] = float4(material.coatWeight, material.coatRoughness,
        material.coatIor, material.thinFilmThickness);
    Results[offset + 1u] = float4(material.coatTint, material.thinFilmIor);
    Results[offset + 2u] = float4(material.coatNormal, material.sheenWeight);
    Results[offset + 3u] = float4(material.sheenTint, material.sheenRoughness);
    Results[offset + 4u] = float4(material.tangent, material.anisotropy);
    Results[offset + 5u] = float4(state.alpha, material.anisotropyRotation, material.alpha);
    Results[offset + 6u] = float4(integral.base.singleScatter, integral.base.directionalAlbedo);
    Results[offset + 7u] = float4(integral.base.averageFresnel, 0.0);
    Results[offset + 8u] = float4(integral.coat.singleScatter, integral.coat.directionalAlbedo);
    Results[offset + 9u] = float4(integral.coat.averageFresnel, 0.0);
    Results[offset + 10u] = float4(state.sheenLtc, state.sheenTransmission);
    Results[offset + 11u] = float4(state.lowerTransmission, 0.0);
    Results[offset + 12u] = float4(state.baseMultipleScatter, 0.0);
    Results[offset + 13u] = float4(state.coatMultipleScatter, 0.0);
    Results[offset + 14u] = float4(direct, 0.0);
    Results[offset + 15u] = float4(ambient.diffuse, 0.0);
    Results[offset + 16u] = float4(ambient.specular, 0.0);
    Results[offset + 17u] = float4(ambient.coat, 0.0);
    Results[offset + 18u] = float4(ambient.sheen, 0.0);
    Results[offset + 19u] = float4(ambient.emission, 0.0);
    Results[offset + 20u] = float4(EvaluatePrincipledFresnel(state.reflection, cosine), 0.0);
    Results[offset + 21u] = float4(ambient.Sum(), 0.0);
    Results[offset + 22u] = float4(PrincipledAnisotropicDistribution(halfVector, material.normal,
        state.tangent, state.bitangent, state.alpha),
        PrincipledAnisotropicVisibility(view, light, material.normal,
            state.tangent, state.bitangent, state.alpha), 0.0, 0.0);
    // Direct lights ignore AO, and respond linearly to incident radiance.
    Results[offset + 23u] = float4(EvaluateLayeredLight(state, light, float3(2.0)), 0.0);
    Results[offset + 24u] = float4(EvaluatePrincipledFresnel(state.reflection, 0.0), 0.0);
    Results[offset + 25u] = float4(EvaluatePrincipledFresnel(state.reflection, 1.0), 0.0);
}
