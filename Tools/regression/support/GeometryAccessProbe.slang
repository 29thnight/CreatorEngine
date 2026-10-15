#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/LxMeshletGpuContract.slang"
ByteAddressBuffer topology : register(t0);
StructuredBuffer<uint2> requests : register(t1);
RWStructuredBuffer<uint4> results : register(u0);
cbuffer Constants : register(b0) { uint4 counts; };
[shader("compute")]
[numthreads(32, 1, 1)]
void GeometryAccessCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= counts.x) return;
    const uint2 request = requests[id.x];
    // This boundary rejects invalid work before touching a descriptor/triangle.
    if (request.x >= topology.Load(8)) {
        results[id.x * 4] = uint4(0xffffffffu); return;
    }
    const LXMeshletDescriptor descriptor = LXLoadMeshlet(topology, request.x);
    if (request.y >= descriptor.triangleCount) {
        results[id.x * 4] = uint4(0xffffffffu); return;
    }
    const LXGeometryPrimitive p = LXLoadGeometryPrimitive(topology, descriptor, request.y);
    results[id.x * 4] = uint4(p.clusterId, p.lodIndex, p.primitiveId, 1);
    results[id.x * 4 + 1] = uint4(p.corners, 0);
    results[id.x * 4 + 2] = uint4(p.vertices, 0);
    results[id.x * 4 + 3] = uint4(p.sourceVertices, 0);
}
