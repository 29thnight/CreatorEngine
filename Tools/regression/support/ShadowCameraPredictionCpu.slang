// 검증 runner가 생산 타입과 함수의 정확한 본문을 현재 snapshot에서 채운다.
// GPU 자원 바인딩만 CPU ITexture/StructuredBuffer adapter로 대신한다.
#include "Includes/GeometryInstanceBounds.slang"
#include "GeometryOcclusion.slang"

/*__VISIBILITY_TYPES__*/

StructuredBuffer<MeshletViewData> predictionViews : register(t100);
StructuredBuffer<float4> predictionBounds : register(t101);
RWStructuredBuffer<uint2> predictionResults : register(u100);

MeshletViewData MeshletView(uint bin)
{
#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
    return predictionViews[geometryVisibilityCandidates[bin].bin];
#else
    return predictionViews[bin];
#endif
}

#if defined(GEOMETRY_VISIBILITY_SHARED_VIEWS)
StructuredBuffer<GeometryVisibilityCandidate> geometryVisibilityCandidates : register(t102);
StructuredBuffer<uint> meshletViewFirst[8] : register(t8213);
Texture2D<float> meshletViewHzb[128] : register(t16384);
/*__SHARED_OCCLUSION__*/
#else
cbuffer PredictionProjection : register(b10)
{
    float4 meshletHistoryColumns[4];
};
StructuredBuffer<uint> meshletFirstStage : register(t8210);
#endif

/*__MESHLET_OCCLUSION__*/

[shader("compute")][numthreads(1, 1, 1)] void ShadowCameraPredictionCpu(uint3 id : SV_DispatchThreadID)
{
    const uint bin = id.x;
    const float3 minimum = predictionBounds[bin * 2u].xyz;
    const float3 maximum = predictionBounds[bin * 2u + 1u].xyz;
    const bool occluded = GeometryMeshletBoxOccluded(bin, minimum, maximum, 0u);
    const MeshletViewData view = MeshletView(bin);
    const bool contained = GeometryPredictionContainsBounds(minimum, maximum, view.history);
    predictionResults[bin] = uint2(uint(occluded), uint(contained));
}
