#ifndef LX_MATERIAL_PACKET_SCAN
#define LX_MATERIAL_PACKET_SCAN
// Shared ABI with PacketDispatchWork::Constants. Count storage and all scan
// levels are bounded by CPU capacities; there is no GPU-to-CPU count readback.
cbuffer LXPacketConstants : register(b0)
{
    uint lx_input_count; uint lx_bucket_count; uint lx_packet_capacity; uint lx_dispatch_width;
    uint lx_scan_count; uint lx_scan_source; uint lx_scan_destination; uint lx_scan_sums;
    uint lx_scan_parent; uint lx_scan_base; uint lx_packet_width; uint lx_packet_height;
    uint lx_slice_capacity; uint3 lx_packet_padding;
};
RWStructuredBuffer<uint> lx_indirect_commands : register(u0);
RWStructuredBuffer<uint4> lx_packet_ranges : register(u1);
RWStructuredBuffer<uint2> lx_packets : register(u2);
RWStructuredBuffer<uint> lx_packet_scan : register(u3);
groupshared uint lx_scan_shared[256];

// The padded final 2D row may extend past UINT32_MAX. Saturate to the
// out-of-range sentinel before converting to a buffer index.
uint LXPacketThread(uint3 id)
{
    return uint(min(uint64_t(id.x) + uint64_t(id.y) * (65535u * 64u), uint64_t(0xffffffffu)));
}
uint LXPacketGroup(uint3 group)
{
    return uint(min(uint64_t(group.x) + uint64_t(group.y) * 65535u, uint64_t(0xffffffffu)));
}
void LXPacketReset(uint3 id)
{
    const uint bucket = LXPacketThread(id);
    if (bucket < lx_bucket_count) lx_packet_scan[bucket] = 0;
}
void LXPacketScan(uint3 group, uint lane)
{
    const uint block = LXPacketGroup(group);
    if (block >= 1u + (lx_scan_count - 1u) / 256u) return;
    const uint index = block * 256u + lane;
    const uint value = index < lx_scan_count ? lx_packet_scan[lx_scan_source + index] : 0;
    lx_scan_shared[lane] = value;
    GroupMemoryBarrierWithGroupSync();
    for (uint offset = 1; offset < 256u; offset <<= 1u)
    {
        const uint add = lane >= offset ? lx_scan_shared[lane - offset] : 0;
        GroupMemoryBarrierWithGroupSync();
        lx_scan_shared[lane] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    if (index < lx_scan_count) lx_packet_scan[lx_scan_destination + index] = lx_scan_shared[lane] - value;
    if (lane == 255u) lx_packet_scan[lx_scan_sums + block] = lx_scan_shared[lane];
}
void LXPacketAddOffsets(uint3 id)
{
    const uint index = LXPacketThread(id);
    if (index < lx_scan_count)
        lx_packet_scan[lx_scan_destination + index] += lx_packet_scan[lx_scan_parent + index / 256u];
}
void LXPacketBuildArguments(uint3 id, ByteAddressBuffer plans)
{
    const uint bucket = LXPacketThread(id);
    if (bucket >= lx_bucket_count) return;
    const uint count = lx_packet_scan[bucket];
    lx_packet_ranges[bucket] = uint4(lx_packet_scan[lx_scan_base + bucket], count, 0, 0);
    const uint4 plan = plans.Load4(bucket * 16u);
    for (uint slice = 0; slice < plan.y; ++slice)
    {
        const uint begin = slice * lx_slice_capacity;
        const uint remaining = begin < count ? min(count - begin, lx_slice_capacity) : 0;
        const uint argument = (plan.x + slice) * 3u;
        lx_indirect_commands[argument] = min(remaining, lx_dispatch_width);
        lx_indirect_commands[argument + 1u] = remaining == 0 ? 1u : 1u + (remaining - 1u) / lx_dispatch_width;
        lx_indirect_commands[argument + 2u] = 1;
    }
}
#endif
