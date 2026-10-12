#ifndef LX_MESHLET_GPU_CONTRACT
#define LX_MESHLET_GPU_CONTRACT
// RHIMeshletBufferHeader schema 2 (80 bytes), MeshletDescriptor profile 2.
// Addresses are bytes. Cooked triangle corners remain packed uint8.
struct LXMeshletDescriptor
{
    uint vertices;
    uint triangles;
    uint primitives;
    uint vertexCount;
    uint triangleCount;
    uint clusterId;
    uint lodIndex;
};
LXMeshletDescriptor LXLoadMeshlet(ByteAddressBuffer data, uint index)
{
    const uint descriptor = data.Load(12) + index * data.Load(16);
    LXMeshletDescriptor value;
    value.vertices = data.Load(20) + data.Load(descriptor) * 4u;
    value.triangles = data.Load(28) + data.Load(descriptor + 4u);
    value.primitives = data.Load(36) + data.Load(descriptor + 8u) * 4u;
    value.vertexCount = data.Load(descriptor + 12u);
    value.triangleCount = data.Load(descriptor + 16u);
    value.clusterId = data.Load(descriptor + 24u);
    value.lodIndex = data.Load(56);
    return value;
}
uint LXMeshletVertex(ByteAddressBuffer data, LXMeshletDescriptor descriptor, uint vertex)
{
    return data.Load(descriptor.vertices + vertex * 4u);
}
uint LXMeshletByte(ByteAddressBuffer data, uint address)
{
    return (data.Load(address & ~3u) >> ((address & 3u) * 8u)) & 255u;
}
uint3 LXMeshletTriangle(ByteAddressBuffer data, LXMeshletDescriptor descriptor, uint triangle)
{
    const uint address = descriptor.triangles + triangle * 3u;
    return uint3(LXMeshletByte(data, address), LXMeshletByte(data, address + 1u), LXMeshletByte(data, address + 2u));
}
uint LXMeshletPrimitiveId(ByteAddressBuffer data, LXMeshletDescriptor descriptor, uint triangle)
{
    return data.Load(descriptor.primitives + triangle * 4u);
}
#endif
