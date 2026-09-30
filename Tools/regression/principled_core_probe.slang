#define LX_MATERIAL_FEATURE_MASK 0x007Fu
#include "../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PbrSurface.slang"

struct ProbeInput
{
    float4 baseAlpha;
    float4 normalRoughness;
    float4 metalIorLevelAo;
    float4 tint;
    float4 emissionColorStrength;
    float4 options;
};

StructuredBuffer<ProbeInput> Inputs : register(t0);
RWStructuredBuffer<float4> Results : register(u0);

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    const ProbeInput probe = Inputs[id.x];
    const float3 geometricNormal = probe.options.y != 0.0
        ? float3(0.0, 0.6, 0.8) : float3(0.0, 0.0, 1.0);
    MaterialInputs inputs = DefaultMaterialInputs(geometricNormal);
    if (probe.options.x == 0.0)
    {
        inputs.baseColor = probe.baseAlpha.rgb;
        inputs.alpha = probe.baseAlpha.a;
        inputs.normal = probe.normalRoughness.xyz;
        inputs.roughness = probe.normalRoughness.w;
        inputs.metallic = probe.metalIorLevelAo.x;
        inputs.ior = probe.metalIorLevelAo.y;
        inputs.specularIorLevel = probe.metalIorLevelAo.z;
        inputs.occlusion = probe.metalIorLevelAo.w;
        inputs.specularTint = probe.tint.rgb;
        inputs.emissionColor = probe.emissionColorStrength.rgb;
        inputs.emissionStrength = probe.emissionColorStrength.w;
    }

    const float cosines[9] = { 0.0, 1.0 / 7.0, 0.25, 0.5, 0.65, 0.75, 0.9, 0.999, 1.0 };
    const float cosine = cosines[id.y];
    const PrincipledSurface material = EvaluateMaterial(inputs, geometricNormal);
    const float3 view = float3(0.6, 0.0, 0.8);
    const PbrSurface surface = BuildPbrSurface(float3(0.0), view, material, PbrNeutralDfg());
    const float3 direct = surface.EvaluateLight(float3(0.0, 0.0, 1.0), float3(1.0));
    const PrincipledIblIntegral integral = IntegratePrincipledIbl(material, cosine);
    const IblAmbient ambient = EvaluatePrincipledIbl(material, cosine,
        float3(1.0), float3(1.0), integral);
    const uint offset = (id.x * 9u + id.y) * 12u;
    Results[offset + 0u] = float4(material.baseColor, material.alpha);
    Results[offset + 1u] = float4(material.normal, material.roughness);
    Results[offset + 2u] = float4(material.metallic, material.ior,
        material.specularIorLevel, material.occlusion);
    Results[offset + 3u] = float4(material.emission, 0.0);
    Results[offset + 4u] = float4(material.dielectricF0, 0.0);
    Results[offset + 5u] = float4(material.metalF0, 0.0);
    Results[offset + 6u] = float4(EvaluatePrincipledFresnel(
        BuildPrincipledReflection(material), cosine), 0.0);
    Results[offset + 7u] = float4(direct, 0.0);
    Results[offset + 8u] = float4(ambient.diffuse, 0.0);
    Results[offset + 9u] = float4(ambient.specular, 0.0);
    Results[offset + 10u] = float4(integral.singleScatter, integral.directionalAlbedo);
    Results[offset + 11u] = float4(integral.averageFresnel, 0.0);
}
