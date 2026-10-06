#define LX_MATERIAL_FEATURE_MASK 0x3FFFu
#define LX_MATERIAL_ROUTE 2
#include "Includes/PrincipledIblLookup.slang"
#include "Includes/SpecialForwardSurface.slang"
#include "Includes/MaterialGraphSceneRefraction.slang"
#ifdef LX_SCENE_REFRACTION_VOLUME
#include "Includes/MaterialGraphSceneVolumeLighting.slang"
#endif

cbuffer LXSceneRefractionConstants : register(b0)
{
    float4x4 lx_viewProjection, lx_inverseViewProjection;
    uint lx_width, lx_height, lx_first, lx_dispatchCount;
    uint lx_environment, lx_steps, lx_rays, lx_reserved;
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
Texture2D<float4> lx_positionIor : register(t11);
Texture2D<float4> lx_normalFilmIor : register(t12);
Texture2D<float4> lx_background : register(t13);
Texture2D<float> lx_depth : register(t14);
Texture2D<uint> lx_owners : register(t15);
TextureCube<float4> gEnvironment : register(t16);
SamplerState gEnvironmentSampler : register(s0);
RWStructuredBuffer<LXSceneRefractionSample> lx_samples : register(u0);
#include "Includes/PrincipledEnvironmentBake.slang"

#include "Includes/MaterialGraphRefractionTransport.slang"

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSceneRefractionBake(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_dispatchCount)
    {
        return;
    }
    const uint index = lx_first + id.x;
    const int2 pixel = int2(index % lx_width, index / lx_width);
    LXSceneRefractionSample sample = (LXSceneRefractionSample)0;
    const float4 positionIor = lx_positionIor.Load(int3(pixel, 0));
    if ((lx_owners.Load(int3(pixel, 0)) & 0x80000000u) == 0 || positionIor.w <= 0)
    {
        lx_samples[index] = sample;
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
        lx_samples[index] = sample;
        return;
    }
    PrincipledSurface material = EvaluateIblBakePoint(point);
    const float4 normalFilm = lx_normalFilmIor.Load(int3(pixel, 0));
    material.normal = normalFilm.xyz;
    material.transmissionIor = positionIor.w;
    material.transmissionFilmIor = normalFilm.w;
    material.transmissionF0 = saturate(pow((positionIor.w - 1) / (positionIor.w + 1), 2)
        * max(point.tintAnisotropy.rgb, 0));
    const PrincipledSurface glass = SpecialGlass(material);
    const float3 view = normalize(point.viewTier.xyz);
    // Match IntegrateSpecialForward's separate glass/transmission budget.
    const PrincipledIblIntegral integral = IntegrateLayeredReflection(glass, view, false, false);
    sample.glassSingleAlbedo = float4(integral.singleScatter, integral.directionalAlbedo);
    sample.glassAverage = float4(integral.averageFresnel, 1);
    sample.rawTransmission = float4(IntegrateSpecialTransmission(glass, view), 0);
    if (lx_environment != 0)
    {
        sample.glassEnvironment = float4(BakeReflection(glass, view, true, false), 0);
    }
    float hitFraction;
    sample.radiance.rgb = LXRefractionTransport(glass, view, positionIor.xyz, hitFraction);
    sample.radiance.w = hitFraction;
    lx_samples[index] = sample;
}
