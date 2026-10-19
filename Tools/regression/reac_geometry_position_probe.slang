// Both position helpers execute from the production include. The complete
// surface attributes are exported to prevent dead-code elimination of that path.
#include "Includes/MeshletGeometryAccess.slang"

StructuredBuffer<float4> positionPose;
ByteAddressBuffer positionTopology;
RWStructuredBuffer<float4> positionResults;

[shader("compute")][numthreads(1, 1, 1)] void PositionOracleCS(uint3 id : SV_DispatchThreadID)
{
    const LXSurfacePoint full = MeteorGeometryPoint(positionPose, positionTopology, id.x);
    positionResults[id.x * 6u] = float4(MeteorGeometryPosition(positionPose, positionTopology, id.x), 0.0f);
    positionResults[id.x * 6u + 1u] = full.position;
    positionResults[id.x * 6u + 2u] = full.uvLod;
    positionResults[id.x * 6u + 3u] = full.normal;
    positionResults[id.x * 6u + 4u] = full.tangent;
    positionResults[id.x * 6u + 5u] = full.bitangent;
}
