#ifndef LX_MESHLET_GPU_CONTRACT
#define LX_MESHLET_GPU_CONTRACT
// RHIMeshletBufferHeader schema 4 (80 bytes, optional spatial replacement DAG section), MeshletDescriptor profile 2.
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
    uint groupId;
    uint primitiveBase;
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
    value.primitiveBase = data.Load(76) != 0u ? data.Load(descriptor + 60u) : 0u;
    const uint hierarchy=data.Load(76);
    value.groupId=hierarchy!=0u ? data.Load(descriptor+28u) : 0u;
    value.lodIndex=hierarchy!=0u ? data.Load(hierarchy+16u+value.groupId*64u+20u) : data.Load(56);
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
    return data.Load(descriptor.primitives + triangle * 4u) - descriptor.primitiveBase;
}
// Shared HW/SW geometry access. IDs are cooked geometry/LOD ordinals, never
// descriptor indices, GPU addresses or visible-list positions. The caller owns
// the sealed topology and posed-world vertex streams through GPU completion.
struct LXGeometryPrimitive
{
    uint3 corners;          // Meshlet-local indices, exact cooked winding.
    uint3 vertices;         // Chunk-local indices into the posed vertex stream.
    uint3 sourceVertices;   // Original vertices within this geometry LOD.
    uint primitiveId;
    uint clusterId;
    uint lodIndex;
    uint primitiveBase;
};
LXGeometryPrimitive LXLoadGeometryPrimitive(ByteAddressBuffer data, LXMeshletDescriptor descriptor, uint triangle)
{
    LXGeometryPrimitive value;
    value.corners = LXMeshletTriangle(data, descriptor, triangle);
    value.vertices = uint3(LXMeshletVertex(data, descriptor, value.corners.x),
        LXMeshletVertex(data, descriptor, value.corners.y), LXMeshletVertex(data, descriptor, value.corners.z));
    // sourceVertexRemap parallels the meshlet vertex remap, not the chunk vertices.
    const uint source = data.Load(64) + (descriptor.vertices - data.Load(20));
    value.sourceVertices = uint3(data.Load(source + value.corners.x * 4u),
        data.Load(source + value.corners.y * 4u), data.Load(source + value.corners.z * 4u));
    value.primitiveId = LXMeshletPrimitiveId(data, descriptor, triangle);
    const uint hierarchy = data.Load(76);
    const uint groupFirst = hierarchy != 0u ? data.Load(hierarchy + 16u + descriptor.groupId * 64u + 48u) : 0u;
    value.clusterId = descriptor.clusterId - groupFirst;
    value.lodIndex = descriptor.lodIndex;
    return value;
}
#endif
