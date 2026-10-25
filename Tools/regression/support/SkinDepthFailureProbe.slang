#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/LxMeshletGpuContract.slang"
#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MeshletGeometryAccess.slang"
StructuredBuffer<float4> pose : register(t0);
ByteAddressBuffer topology : register(t1);
StructuredBuffer<uint4> query : register(t2);
RWStructuredBuffer<float4> result : register(u0);
[numthreads(1, 1, 1)]
void SkinDepthFailureCS()
{
    const uint4 selected = query[0];
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(topology, selected.x);
    const LXGeometryPrimitive primitive = LXLoadGeometryPrimitive(topology, descriptor, selected.y);
    for (uint corner = 0u; corner < 3u; ++corner)
    {
        const uint vertex = primitive.vertices[corner];
        result[corner] = float4(MeteorGeometryPoint(pose, topology, vertex).position.xyz, asfloat(vertex));
    }
}
