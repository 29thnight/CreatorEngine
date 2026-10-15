// Visibility-driven material tile work. Counts and list capacity are bounded by
// the viewport pixel count: a record can appear at most once in each 8x8 tile.
#include "Includes/MeteorVisibilityContract.slang"
StructuredBuffer<LXFrameGeometryRecord> geometryTable : register(t3);
cbuffer MaterialWorkConstants : register(b0) { uint width; uint height; uint recordCount; uint commandCount; };
Texture2D<uint2> visibility : register(t0);
StructuredBuffer<uint4> commands : register(t1);
ByteAddressBuffer templates : register(t2);
RWStructuredBuffer<uint> work : register(u0);
RWStructuredBuffer<uint> arguments : register(u1);
groupshared uint records[64];

[numthreads(64,1,1)]
void ResetMaterialWork(uint3 id : SV_DispatchThreadID)
{
    if (id.x < recordCount) work[4 + id.x] = 0;
    if (id.x == 0) { work[0] = width; work[1] = height; work[2] = recordCount; work[3] = 8; }
}

bool TileRecord(uint lane, uint2 tile, out uint record)
{
    uint2 pixel = tile * 8 + uint2(lane & 7, lane >> 3);
    record = all(pixel < uint2(width, height)) ? visibility.Load(int3(pixel, 0)).x : 0;
    record = MeteorVisibilityRecord(geometryTable, recordCount, record);
    records[lane] = record;
    GroupMemoryBarrierWithGroupSync();
    bool unique = record != 0 && record < recordCount;
    for (uint previous = 0; previous < lane; ++previous) unique = unique && records[previous] != record;
    return unique;
}
[numthreads(64,1,1)]
void CountMaterialTiles(uint lane : SV_GroupIndex, uint3 tile : SV_GroupID)
{
    uint record;
    if (TileRecord(lane, tile.xy, record)) InterlockedAdd(work[4 + record], 1);
}
// A single deterministic scan establishes disjoint ranges; no readback or CPU binning.
[numthreads(1,1,1)]
void BuildMaterialRanges(uint3 id : SV_DispatchThreadID)
{
    uint offset = 0;
    for (uint record = 0; record < recordCount; ++record) {
        work[4 + recordCount + record] = offset;
        work[4 + 2 * recordCount + record] = 0;
        offset += work[4 + record];
    }
    for (uint index = 0; index < commandCount; ++index) {
        uint4 command = commands[index]; // template offset, output offset, root words, record ID
        for (uint word = 0; word < command.z; ++word)
            arguments[command.y / 4 + word] = templates.Load(command.x + word * 4);
        uint count = work[4 + command.w];
        arguments[command.y / 4 + command.z] = min(count, 65535u);
        arguments[command.y / 4 + command.z + 1] = max(1u, (count + 65534u) / 65535u);
        arguments[command.y / 4 + command.z + 2] = 1;
    }
}
[numthreads(64,1,1)]
void ScatterMaterialTiles(uint lane : SV_GroupIndex, uint3 tile : SV_GroupID)
{
    uint record;
    if (TileRecord(lane, tile.xy, record)) {
        uint local;
        InterlockedAdd(work[4 + 2 * recordCount + record], 1, local);
        uint offset = work[4 + recordCount + record];
        work[4 + 3 * recordCount + offset + local] = tile.y * ((width + 7) / 8) + tile.x;
    }
}
