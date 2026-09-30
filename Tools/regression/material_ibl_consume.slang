#define LX_MATERIAL_FEATURE_MASK 0x07FFu
#include "PrincipledIblLookup.slang"

StructuredBuffer<PrincipledIblBakePoint> gPoints : register(t0);
StructuredBuffer<PrincipledIblBakeSample> gSamples : register(t1);

[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(vertices[id], 0, 1);
}

[shader("fragment")]
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    const uint index = uint(position.x);
    const PrincipledIblBakePoint point = gPoints[index];
    return float4(EvaluateBakedPrincipledIbl(EvaluateIblBakePoint(point), normalize(point.viewTier.xyz),
        point.viewTier.w == 1.0, gSamples[index]), 1);
}
