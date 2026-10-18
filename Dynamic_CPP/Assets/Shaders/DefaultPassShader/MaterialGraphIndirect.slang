// Each frame supplies immutable root descriptors; GPU visibility supplies work.
// No material instance or mesh is submitted separately by the raster consumer.
#include "Includes/MaterialGraphPacketScan.slang"
StructuredBuffer<uint4> lx_commands : register(t0);
ByteAddressBuffer lx_templates : register(t1);
ByteAddressBuffer lx_visible_arguments : register(t2);

[numthreads(64, 1, 1)]
void LXBuildIndirectCommands(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_input_count) return;
    uint4 command = lx_commands[id.x]; // template byte offset, output byte offset, root words, args byte offset
    for (uint word = 0; word < command.z; ++word)
        lx_indirect_commands[command.y / 4 + word] = lx_templates.Load(command.x + word * 4);
    uint3 groups = lx_visible_arguments.Load3(command.w);
    for (uint axis = 0; axis < 3; ++axis) lx_indirect_commands[command.y / 4 + command.z + axis] = groups[axis];
}

[numthreads(64, 1, 1)]
void LXResetPacketBuckets(uint3 id : SV_DispatchThreadID) { LXPacketReset(id); }

[numthreads(64, 1, 1)]
void LXCountPacketBuckets(uint3 id : SV_DispatchThreadID)
{
    const uint index = LXPacketThread(id);
    if (index >= lx_input_count) return;
    const uint4 draw = lx_commands[index]; // record, visible word offset, bucket, capacity
    const uint count = min(lx_templates.Load(draw.y * 4u), draw.w);
    InterlockedAdd(lx_packet_scan[draw.z], count);
}
[numthreads(256, 1, 1)]
void LXScanPacketRanges(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) { LXPacketScan(group, lane); }

[numthreads(64, 1, 1)]
void LXAddPacketRangeOffsets(uint3 id : SV_DispatchThreadID) { LXPacketAddOffsets(id); }

[numthreads(64, 1, 1)]
void LXBuildPacketArguments(uint3 id : SV_DispatchThreadID) { LXPacketBuildArguments(id, lx_visible_arguments); }

groupshared uint lx_packet_draw_base;
[numthreads(64, 1, 1)]
void LXScatterPackets(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    const uint index = LXPacketGroup(group);
    if (index >= lx_input_count) return;
    const uint4 draw = lx_commands[index];
    const uint count = min(lx_templates.Load(draw.y * 4u), draw.w);
    if (lane == 0u) InterlockedAdd(lx_packet_ranges[draw.z].z, count, lx_packet_draw_base);
    GroupMemoryBarrierWithGroupSync();
    const uint base = lx_packet_ranges[draw.z].x + lx_packet_draw_base;
    for (uint item = lane; item < count; item += 64u)
        lx_packets[base + item] = uint2(draw.x, lx_templates.Load((draw.y + 1u + item) * 4u));
}
