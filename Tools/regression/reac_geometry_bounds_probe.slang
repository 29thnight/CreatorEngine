// CPU target fixture: execute the production helpers without translating them.
#include "Includes/GeometryInstanceBounds.slang"

struct BoundsOracleInput
{
    float4 minimum;
    float4 maximum;
    float4 transform[3];
    float4 projection[4];
    uint4 control;
};

struct BoundsOracleOutput
{
    float4 minimum;
    float4 maximum;
    uint4 classification;
};

StructuredBuffer<BoundsOracleInput> boundsInputs;
RWStructuredBuffer<BoundsOracleOutput> boundsOutputs;

[shader("compute")][numthreads(1, 1, 1)] void BoundsOracleCS(uint3 id : SV_DispatchThreadID)
{
    const BoundsOracleInput input = boundsInputs[id.x];
    BoundsOracleOutput output = (BoundsOracleOutput)0;
    float3 minimum, maximum;
    output.classification.x =
        uint(GeometryTransformInstanceBounds(input.minimum.xyz, input.maximum.xyz, input.transform, minimum, maximum));
    output.minimum = float4(minimum, 0.0f);
    output.maximum = float4(maximum, 0.0f);
    output.classification.y =
        uint(GeometryInstanceOutsideFrustum(input.minimum.xyz, input.maximum.xyz, input.projection, input.control.x));
    boundsOutputs[id.x] = output;
}
