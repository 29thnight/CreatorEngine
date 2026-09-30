#define LX_MATERIAL_FEATURE_MASK 0x07FFu
#include "Includes/PrincipledIblLookup.slang"

cbuffer BakeConstants : register(b0)
{
    uint gPointCount;
    uint3 gReserved;
};
StructuredBuffer<PrincipledIblBakePoint> gPoints : register(t0);
TextureCube<float4> gEnvironment : register(t1);
SamplerState gEnvironmentSampler : register(s0);
RWStructuredBuffer<PrincipledIblBakeSample> gOutput : register(u0);

#include "Includes/PrincipledEnvironmentBake.slang"

[shader("compute")][numthreads(32, 1, 1)] void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gPointCount)
    {
        return;
    }
    const PrincipledIblBakePoint point = gPoints[id.x];
    if (!IsValidPrincipledIblBakePoint(point))
    {
        PrincipledIblBakeSample rejected = (PrincipledIblBakeSample)0;
        rejected.baseAverage.w = -1.0;
        gOutput[id.x] = rejected;
        return;
    }
    const PrincipledSurface material = EvaluateIblBakePoint(point);
    const float3 view = normalize(point.viewTier.xyz);
    const bool layered = point.viewTier.w == 1.0;
    PrincipledLayeredIntegral integral;
    integral.base = layered ? IntegrateLayeredReflection(material, view, false)
                            : IntegratePrincipledIbl(material, saturate(dot(material.normal, view)));
    integral.coat = layered && material.coatWeight > 0.0 ? IntegrateLayeredReflection(material, view, true)
                                                         : PrincipledEmptyIntegral();
    PrincipledIblBakeSample sample;
    sample.baseSingleAlbedo = float4(integral.base.singleScatter, integral.base.directionalAlbedo);
    sample.baseAverage = float4(integral.base.averageFresnel, 0);
    sample.coatSingleAlbedo = float4(integral.coat.singleScatter, integral.coat.directionalAlbedo);
    sample.coatAverage = float4(integral.coat.averageFresnel, 0);
    sample.irradiance = float4(BakeDiffuse(material.normal), 0);
    sample.basePrefiltered = float4(BakeReflection(material, view, layered, false), 0);
    sample.coatPrefiltered =
        float4(layered && material.coatWeight > 0.0 ? BakeReflection(material, view, true, true) : float3(0), 0);
    sample.coatIrradiance =
        float4(layered && material.coatWeight > 0.0 ? BakeDiffuse(material.coatNormal) : float3(0), 0);
    sample.sheenIrradiance = float4(layered ? BakeSheen(BuildLayeredState(material, view, integral)) : float3(0), 0);
    gOutput[id.x] = sample;
}
