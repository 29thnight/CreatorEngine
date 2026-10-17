#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MeshletGeometryAccess.slang"
cbuffer Request : register(b0) { uint4 request; };
StructuredBuffer<float4> pose : register(t0);
ByteAddressBuffer topology : register(t1);
RWStructuredBuffer<LXSurfacePoint> points : register(u0);
RWStructuredBuffer<uint> attributes : register(u1);
[numthreads(64,1,1)]
void ClusterStorageCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= request.x)
    {
        return;
    }
    points[id.x] = MeteorGeometryPoint(pose,topology,id.x);
    const uint2 map = topology.Load2(topology.Load(80u)+id.x*8u);
    for (uint channel = 0u; channel < request.y; ++channel)
    {
        attributes[id.x*request.y+channel] = MeteorClusterWord(topology,map.x,map.y,channel*4u);
    }
}
