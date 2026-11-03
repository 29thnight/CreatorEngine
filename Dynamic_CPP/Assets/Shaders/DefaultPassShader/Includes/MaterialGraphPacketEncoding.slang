#ifndef LX_MATERIAL_PACKET_ENCODING
#define LX_MATERIAL_PACKET_ENCODING
uint2 LXLoadMaterialPacket(StructuredBuffer<uint2> packets, uint index, uint tileBits)
{
    if (tileBits == 0u)
    {
        return packets[index];
    }
    const uint word = packets[index >> 1u][index & 1u];
    return uint2(word >> tileBits, word & ((1u << tileBits) - 1u));
}
#endif
