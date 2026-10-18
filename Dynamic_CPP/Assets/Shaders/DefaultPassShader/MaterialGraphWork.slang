// The pixel count bounds packet capacity: a record occurs at most once in an
// 8x8 tile, and each unique record has at least one contributing pixel.
#include "Includes/MeteorVisibilityContract.slang"
#include "Includes/MaterialGraphSceneRecord.slang"
#include "Includes/MaterialGraphPacketScan.slang"
Texture2D<uint2> visibility : register(t0);
StructuredBuffer<LXSceneGpuRecord> sceneRecords : register(t1);
StructuredBuffer<LXFrameGeometryRecord> geometryTable : register(t2);
ByteAddressBuffer bucketPlans : register(t3);
groupshared uint records[64];

[numthreads(64, 1, 1)]
void ResetMaterialWork(uint3 id : SV_DispatchThreadID) { LXPacketReset(id); }

bool TileRecord(uint lane, uint2 tile, out uint record, out uint bucket)
{
    const uint2 pixel = tile * 8 + uint2(lane & 7, lane >> 3);
    const uint primitive = all(pixel < uint2(lx_packet_width, lx_packet_height))
        ? visibility.Load(int3(pixel, 0)).x : 0;
    record = MeteorVisibilityRecord(geometryTable, lx_input_count, primitive);
    bucket = record != 0 && record < lx_input_count ? sceneRecords[record].history.z : 0xffffffffu;
    records[lane] = record;
    GroupMemoryBarrierWithGroupSync();
    bool unique = record != 0 && record < lx_input_count && bucket < lx_bucket_count;
    for (uint previous = 0; previous < lane; ++previous) unique = unique && records[previous] != record;
    return unique;
}
[numthreads(64, 1, 1)]
void CountMaterialTiles(uint lane : SV_GroupIndex, uint3 tile : SV_GroupID)
{
    uint record, bucket;
    if (TileRecord(lane, tile.xy, record, bucket)) InterlockedAdd(lx_packet_scan[bucket], 1);
}
[numthreads(256, 1, 1)]
void ScanMaterialRanges(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) { LXPacketScan(group, lane); }

[numthreads(64, 1, 1)]
void AddMaterialRangeOffsets(uint3 id : SV_DispatchThreadID) { LXPacketAddOffsets(id); }

[numthreads(64, 1, 1)]
void BuildMaterialRanges(uint3 id : SV_DispatchThreadID) { LXPacketBuildArguments(id, bucketPlans); }

[numthreads(64, 1, 1)]
void ScatterMaterialTiles(uint lane : SV_GroupIndex, uint3 tile : SV_GroupID)
{
    uint record, bucket;
    if (TileRecord(lane, tile.xy, record, bucket))
    {
        uint local;
        InterlockedAdd(lx_packet_ranges[bucket].z, 1, local);
        const uint offset = lx_packet_ranges[bucket].x + local;
        lx_packets[offset] = uint2(record, tile.y * ((lx_packet_width + 7) / 8) + tile.x);
    }
}
