#include "Includes/MaterialGraphPrimitiveCache.slang"
#include "Includes/MeshletGeometryAccess.slang"
#include "Includes/LxMeshletGpuContract.slang"

cbuffer LXPrimitiveCacheConstants : register(b0)
{
    float4x4 lx_cacheProjection;
    uint lx_cacheWidth;
    uint lx_cacheHeight;
    uint lx_cacheRecordCount;
    uint lx_cacheCapacity;
};

cbuffer LXPrimitiveCacheSelector : register(b8)
{
    uint4 lx_cacheSelector;
};

Texture2D<uint2> lx_cacheVisibility : register(t0);
StructuredBuffer<LXSceneGpuRecord> lx_cacheRecords : register(t1);
StructuredBuffer<uint2> lx_cachePackets : register(t2);
StructuredBuffer<uint4> lx_cacheRanges : register(t3);
StructuredBuffer<float4> lx_cacheVertices[64] : register(t256);
ByteAddressBuffer lx_cacheTopology[64] : register(t512);
RWStructuredBuffer<uint> lx_cacheKeys : register(u0);
RWStructuredBuffer<LXPrimitiveCacheTriangle> lx_cacheData : register(u1);

[numthreads(64, 1, 1)]
void ClearPrimitiveCache(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x < lx_cacheCapacity)
    {
        lx_cacheKeys[thread.x] = 0u;
    }
}

LXPrimitiveCacheVertex LXPrimitiveCacheTransform(uint vertex, LXSceneGpuRecord record, float4x4 world)
{
    // packet/record는 SV_GroupID와 selector만으로 결정되어 8x8 group 전체에 같다.
    // 주소가 서로 다른 pixel도 같은 geometry descriptor를 사용하므로 nonuniform이 아니다.
    const uint current = record.geometry.x;
    const uint previous = record.geometry.y;
    const uint topology = record.geometry.z;
    LXPrimitiveCacheVertex result;
    result.point = LXPrimitiveWorldPoint(
        MeteorGeometryPoint(lx_cacheVertices[current], lx_cacheTopology[topology], vertex), world);
    result.rasterClip = mul(float4(result.point.position.xyz, 1), lx_cacheProjection);
    // 이전 pose를 object space에 보존한다. Temporal의 보간 후 world/projection 순서를 바꾸지 않는다.
    result.previousPosition =
        float4(MeteorGeometryPosition(lx_cacheVertices[previous], lx_cacheTopology[topology], vertex), 0);
    return result;
}

groupshared uint lx_cacheTileAddresses[64];

[numthreads(8, 8, 1)]
void PopulatePrimitiveCache(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint4 range = lx_cacheRanges[lx_cacheSelector.x];
    const uint64_t flat = uint64_t(lx_cacheSelector.z) + group.x + uint64_t(group.y) * lx_cacheSelector.y;
    const bool validPacket = flat < range.y;
    const uint2 packet = validPacket ? lx_cachePackets[range.x + uint(flat)] : uint2(0u);
    const bool validRecord = packet.x != 0u && packet.x < lx_cacheRecordCount;
    const uint grid = (lx_cacheWidth + 7u) / 8u;
    const uint2 pixel = uint2(packet.y % grid, packet.y / grid) * 8u + uint2(lane & 7u, lane >> 3u);
    const LXSceneGpuRecord record = validRecord ? lx_cacheRecords[packet.x] : (LXSceneGpuRecord)0;
    uint address = 0u;
    if (validPacket && validRecord && pixel.x < lx_cacheWidth && pixel.y < lx_cacheHeight)
    {
        const uint visible = lx_cacheVisibility.Load(int3(pixel, 0)).x;
        if (record.identity.reserved.y != 0u && visible >= record.identity.reserved.y &&
            visible < record.identity.reserved.z)
        {
            address = visible;
        }
    }
    // 유효하지 않은 packet/edge lane도 barrier에 참여한다. 네 개 인접 pixel의
    // 중복 CAS만 줄이고, wave 크기나 vendor subgroup 확장에 의존하지 않는다.
    lx_cacheTileAddresses[lane] = address;
    GroupMemoryBarrierWithGroupSync();
    if (address == 0u)
    {
        return;
    }
    const uint firstLane = lane & ~3u;
    [unroll]
    for (uint preceding = 0u; preceding < 4u; ++preceding)
    {
        if (firstLane + preceding < lane && lx_cacheTileAddresses[firstLane + preceding] == address)
        {
            return;
        }
    }
    const uint first = LXPrimitiveCacheHash(address);
    [loop]
    for (uint probe = 0u; probe < LXPrimitiveCacheProbeCount; ++probe)
    {
        const uint slot = (first + probe) & (LXPrimitiveCacheCapacity - 1u);
        uint found;
        InterlockedCompareExchange(lx_cacheKeys[slot], 0u, address, found);
        if (found == address)
        {
            return;
        }
        if (found == 0u)
        {
            const uint primitiveAddress = address - record.identity.reserved.y;
            const uint topology = record.geometry.z;
            const LXMeshletDescriptor descriptor = LXLoadMeshlet(lx_cacheTopology[topology], primitiveAddress >> 7u);
            const LXGeometryPrimitive primitive =
                LXLoadGeometryPrimitive(lx_cacheTopology[topology], descriptor, primitiveAddress & 127u);
            const float4x4 world = LXPrimitiveWorld(record, false);
            LXPrimitiveCacheTriangle result;
            result.a = LXPrimitiveCacheTransform(primitive.vertices.x, record, world);
            result.b = LXPrimitiveCacheTransform(primitive.vertices.y, record, world);
            result.c = LXPrimitiveCacheTransform(primitive.vertices.z, record, world);
            lx_cacheData[slot] = result;
            return;
        }
    }
    // 고정 probe 한도 초과는 누락된 cache entry일 뿐이다. 소비자가 기존 decode를 실행한다.
}
