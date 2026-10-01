#define LX_MATERIAL_FEATURE_MASK 0x17FFu
#define LX_MATERIAL_ROUTE 2
#include "Includes/PrincipledIblLookup.slang"
#include "Includes/SpecialForwardSurface.slang"
#include "Includes/MaterialGraphSceneSubsurface.slang"

cbuffer LXSceneSubsurfaceConstants : register(b0)
{
    uint lx_width, lx_height, lx_first, lx_dispatchCount;
    uint lx_environment, lx_radius, lx_reserved0, lx_reserved1;
};
Texture2D<float4> lx_inputs_0 : register(t0);
Texture2D<float4> lx_inputs_1 : register(t1);
Texture2D<float4> lx_inputs_2 : register(t2);
Texture2D<float4> lx_inputs_3 : register(t3);
Texture2D<float4> lx_inputs_4 : register(t4);
Texture2D<float4> lx_inputs_5 : register(t5);
Texture2D<float4> lx_inputs_6 : register(t6);
Texture2D<float4> lx_inputs_7 : register(t7);
Texture2D<float4> lx_inputs_8 : register(t8);
Texture2D<float4> lx_inputs_9 : register(t9);
Texture2D<float4> lx_inputs_10 : register(t10);

float4 LXLoadSceneInput(uint field, int3 pixel)
{
    switch (field)
    {
        case 0u:
            return lx_inputs_0.Load(pixel);
        case 1u:
            return lx_inputs_1.Load(pixel);
        case 2u:
            return lx_inputs_2.Load(pixel);
        case 3u:
            return lx_inputs_3.Load(pixel);
        case 4u:
            return lx_inputs_4.Load(pixel);
        case 5u:
            return lx_inputs_5.Load(pixel);
        case 6u:
            return lx_inputs_6.Load(pixel);
        case 7u:
            return lx_inputs_7.Load(pixel);
        case 8u:
            return lx_inputs_8.Load(pixel);
        case 9u:
            return lx_inputs_9.Load(pixel);
        case 10u:
            return lx_inputs_10.Load(pixel);
        default:
            return float4(0);
    }
}
TextureCube<float4> gEnvironment : register(t11);
Texture2D<uint> lx_owners : register(t12);
SamplerState gEnvironmentSampler : register(s0);
RWStructuredBuffer<LXSceneSubsurfaceReflection> lx_reflection : register(u0);
#include "Includes/PrincipledEnvironmentBake.slang"

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneSubsurfaceBake(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_dispatchCount)
    {
        return;
    }
    const uint index = lx_first + id.x;
    const int2 pixel = int2(index % lx_width, index / lx_width);
    LXSceneSubsurfaceReflection sample = (LXSceneSubsurfaceReflection)0;
    if ((lx_owners.Load(int3(pixel, 0)) & 0x80000000u) == 0u)
    {
        lx_reflection[index] = sample;
        return;
    }
    PrincipledIblBakePoint point;
    point.baseAlpha = lx_inputs_0.Load(int3(pixel, 0));
    point.normalRoughness = lx_inputs_1.Load(int3(pixel, 0));
    point.metalIorLevelAo = lx_inputs_2.Load(int3(pixel, 0));
    point.tintAnisotropy = lx_inputs_3.Load(int3(pixel, 0));
    point.emissionColorStrength = lx_inputs_4.Load(int3(pixel, 0));
    point.coatWeightRoughIorFilmThickness = lx_inputs_5.Load(int3(pixel, 0));
    point.coatTintFilmIor = lx_inputs_6.Load(int3(pixel, 0));
    point.coatNormalSheenWeight = lx_inputs_7.Load(int3(pixel, 0));
    point.sheenTintRoughness = lx_inputs_8.Load(int3(pixel, 0));
    point.tangentRotation = lx_inputs_9.Load(int3(pixel, 0));
    point.viewTier = lx_inputs_10.Load(int3(pixel, 0));
    if (!IsValidScenePrincipledIblBakePoint(point))
    {
        lx_reflection[index] = sample;
        return;
    }
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
    if (lx_environment != 0u)
    {
        sample.metalEnvironment = float4(BakeReflection(metal, view, true, false), 0);
        sample.dielectricEnvironment = float4(BakeReflection(dielectric, view, true, false), 0);
    }
    lx_reflection[index] = sample;
}
