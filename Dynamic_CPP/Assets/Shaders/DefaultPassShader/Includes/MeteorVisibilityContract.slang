#ifndef METEOR_VISIBILITY_CONTRACT
#define METEOR_VISIBILITY_CONTRACT
// SceneFrameGeometry::Record, 128 bytes. reserved.yz is the half-open range.
// worldColumns map the object-space vertex stream to world space.
struct LXFrameGeometryRecord
{
    uint2 geometryKey, objectId, incarnation, instanceId, materialSlot;
    uint lodIndex, primitiveCount, version;
    uint3 reserved;
    uint chunkIndex, owner, meshletCount, vertexCount;
    float4 worldColumns[3];
};
float3 MeteorRecordWorldPosition(LXFrameGeometryRecord record, float3 position)
{
    const float4 p = float4(position, 1);
    return float3(dot(p, record.worldColumns[0]), dot(p, record.worldColumns[1]), dot(p, record.worldColumns[2]));
}
uint MeteorVisibilityRecord(StructuredBuffer<LXFrameGeometryRecord> table, uint count, uint address)
{
    if (address == 0u || address == 0xffffffffu) return 0u;
    uint first = 1u, last = count;
    while (first < last) {
        const uint middle = first + (last - first) / 2u;
        const uint2 range = table[middle].reserved.yz;
        if (address < range.x) last = middle;
        else if (address >= range.y) first = middle + 1u;
        else return ((address - range.x) & 127u) < 126u ? middle : 0u;
    }
    return 0u;
}
uint2 MeteorVisibilityPayload(float depth, uint address)
{
    return uint2(address, asuint(depth == 0.0 ? 0.0 : depth));
}
uint64_t MeteorVisibilityKey(float depth, uint address)
{
    const uint2 payload=MeteorVisibilityPayload(depth,address);
    return (uint64_t(payload.y)<<32u)|uint64_t(payload.x);
}
#endif
