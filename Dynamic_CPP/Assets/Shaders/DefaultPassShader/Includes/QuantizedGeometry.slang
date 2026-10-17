// CPU-validated CGQ1 packet; raw offsets still follow ModelVertexLayout.
uint LXGeometryWord(ByteAddressBuffer data, uint packet, uint vertex, uint byteOffset)
{
    if (data.Load(packet + 8u) == 1u)
    {
        return data.Load(packet + 32u + vertex * data.Load(packet + 20u) + byteOffset);
    }
    const uint d = packet + 32u + (byteOffset / 4u) * 24u;
    const uint4 descriptor = data.Load4(d);
    const uint mode = descriptor.x;
    const uint origin = descriptor.z;
    if (mode == 0u)
    {
        return origin;
    }
    const uint width = descriptor.y;
    const uint bit = vertex * width;
    const uint shift = bit & 31u;
    const uint address = packet + data.Load(d + 16u) + (bit / 32u) * 4u;
    uint code = data.Load(address) >> shift;
    if (shift + width > 32u)
    {
        code |= data.Load(address + 4u) << (32u - shift);
    }
    if (width < 32u)
    {
        code &= (1u << width) - 1u;
    }
    if (mode == 1u)
    {
        precise float product = float(code) * asfloat(descriptor.w);
        precise float value = asfloat(origin) + product;
        return asuint(value);
    }
    return code;
}
float2 LXGeometry2(ByteAddressBuffer data, uint packet, uint vertex, uint offset)
{
    return asfloat(uint2(LXGeometryWord(data,packet,vertex,offset),LXGeometryWord(data,packet,vertex,offset+4u)));
}
float3 LXGeometry3(ByteAddressBuffer data, uint packet, uint vertex, uint offset)
{
    return asfloat(uint3(LXGeometryWord(data,packet,vertex,offset),LXGeometryWord(data,packet,vertex,offset+4u),LXGeometryWord(data,packet,vertex,offset+8u)));
}
float4 LXGeometry4(ByteAddressBuffer data, uint packet, uint vertex, uint offset)
{
    return asfloat(uint4(LXGeometryWord(data,packet,vertex,offset),LXGeometryWord(data,packet,vertex,offset+4u),LXGeometryWord(data,packet,vertex,offset+8u),LXGeometryWord(data,packet,vertex,offset+12u)));
}
