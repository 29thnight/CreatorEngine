#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MeshletGeometryAccess.slang"
#include "../../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SkinLodError.slang"
StructuredBuffer<float4> pose : register(t0);
ByteAddressBuffer topology : register(t1);
RWStructuredBuffer<float4> result : register(u0);
[numthreads(1,1,1)]
void SkinLodErrorCS()
{
    float4 sphere = asfloat(topology.Load4(32u));
    float error = asfloat(topology.Load(28u));
    const bool valid = MeteorSkinGroupMetric(pose, topology, 0u, sphere, error);
    result[0] = sphere;
    result[1] = float4(error, valid ? 1.0f : 0.0f, 0.0f, 0.0f);
}
