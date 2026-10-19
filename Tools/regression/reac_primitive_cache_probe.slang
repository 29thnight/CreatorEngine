// 실제 cache 생산자의 변환 함수와 조회 함수를 CPU target으로 실행한다.
#include "MaterialGraphPrimitiveCache.slang"

StructuredBuffer<uint> oracleKeys;
StructuredBuffer<uint> oracleAddresses;
RWStructuredBuffer<uint4> oracleResults;

// 0d3c2cac의 LXSceneWorldPoint 본문이다. 새 helper를 통해 기준식을 재사용하지 않는다.
LXSurfacePoint BaselineWorldPoint(LXSurfacePoint source, float4x4 world)
{
    LXSurfacePoint result = source;
    result.position = float4(mul(float4(source.position.xyz, 1), world).xyz, 0);
    float3 n, t, b;
    TransformPbrFrame(source.normal.xyz, source.tangent.xyz, source.bitangent.xyz, (float3x3)world, n, t, b);
    result.normal = float4(n, 0);
    result.tangent = float4(t, 0);
    result.bitangent = float4(b, 0);
    return result;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void PrimitiveCacheOracleCS(uint3 id : SV_DispatchThreadID)
{
    const uint address = oracleAddresses[id.x];
    uint mismatch = 0u;
    if (id.x < 3u)
    {
        const LXSceneGpuRecord record = lx_cacheRecords[0];
        const float4x4 world = LXPrimitiveWorld(record, false);
        const LXPrimitiveCacheVertex cached = LXPrimitiveCacheTransform(id.x, record, world);
        const LXSurfacePoint current = MeteorGeometryPoint(lx_cacheVertices[0], lx_cacheTopology[0], id.x);
        const LXSurfacePoint baseline = BaselineWorldPoint(current, world);
        const float4 clip = mul(float4(baseline.position.xyz, 1), lx_cacheProjection);
        const LXSurfacePoint previous = MeteorGeometryPoint(lx_cacheVertices[1], lx_cacheTopology[0], id.x);
        mismatch |= any(asuint(cached.point.uvLod) != asuint(baseline.uvLod)) ? 1u : 0u;
        mismatch |= any(asuint(cached.point.position) != asuint(baseline.position)) ? 2u : 0u;
        mismatch |= any(asuint(cached.point.normal) != asuint(baseline.normal)) ? 4u : 0u;
        mismatch |= any(asuint(cached.point.tangent) != asuint(baseline.tangent)) ? 8u : 0u;
        mismatch |= any(asuint(cached.point.bitangent) != asuint(baseline.bitangent)) ? 16u : 0u;
        mismatch |= any(asuint(cached.rasterClip) != asuint(clip)) ? 32u : 0u;
        mismatch |= any(asuint(cached.previousPosition.xyz) != asuint(previous.position.xyz)) ? 64u : 0u;
        mismatch |= asuint(cached.previousPosition.w) != 0u ? 128u : 0u;
    }
    oracleResults[id.x] = uint4(LXPrimitiveCacheHash(address), LXPrimitiveCacheFind(oracleKeys, address), mismatch,
                                LXPrimitiveCacheCapacity);
}
