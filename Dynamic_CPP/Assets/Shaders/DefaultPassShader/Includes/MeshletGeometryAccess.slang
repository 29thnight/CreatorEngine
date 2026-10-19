#pragma once
#include "MaterialGraphSurfacePoint.slang"
#include "MaterialEvaluation.slang"
#include "PackedBones.slang"
#include "QuantizedGeometry.slang"
// Both immutable pose packets and compatibility points are viewed as float4 rows.
// A bone row is one aligned read; never gather it through an 80-byte point.
uint MeteorPoseWord(StructuredBuffer<float4> pose, uint byteOffset)
{
    return asuint(pose[byteOffset / 16u])[(byteOffset % 16u) / 4u];
}
float4 MeteorPose4(StructuredBuffer<float4> pose, uint address)
{
    return pose[address / 16u];
}
float4x4 MeteorBone(StructuredBuffer<float4> pose, uint bone)
{
    const uint address = 32u + bone * 48u;
    PackedBone value;
    value.row0 = MeteorPose4(pose,address);
    value.row1 = MeteorPose4(pose,address+16u);
    value.row2 = MeteorPose4(pose,address+32u);
    return UnpackBone(value);
}
LXSurfacePoint MeteorCompatibilityPoint(StructuredBuffer<float4> data, uint vertex)
{
    const uint row = vertex * 5u;
    LXSurfacePoint point;
    point.uvLod = data[row];
    point.position = data[row+1u];
    point.normal = data[row+2u];
    point.tangent = data[row+3u];
    point.bitangent = data[row+4u];
    return point;
}
uint MeteorClusterWord(ByteAddressBuffer data, uint packet, uint vertex, uint offset)
{
    const uint bits = LXGeometryWord(data,packet+data.Load(packet+16u),vertex,offset);
    const uint descriptor = packet+32u+(offset/4u)*8u;
    const float step = asfloat(data.Load(descriptor+4u));
    if (step == 0.0)
    {
        return bits;
    }
    precise float product = asfloat(bits) * step;
    precise float value = asfloat(data.Load(descriptor)) + product;
    return asuint(value);
}
float4 MeteorClusterChannels(ByteAddressBuffer data,uint packet,uint vertex,uint offset,uint count)
{
    // Keep one bounded decoder body. Surface reconstruction reads three
    // vertices inside quad evaluation; expanding the scalar decoder at every
    // component also expands the validation layer's patched shader.
    float4 result = float4(0);
    [loop] for (uint component=0u; component<count; ++component)
        result[component] = asfloat(MeteorClusterWord(data,packet,vertex,offset+component*4u));
    return result;
}
float3 MeteorCluster3(ByteAddressBuffer data,uint packet,uint vertex,uint offset)
{
    return MeteorClusterChannels(data,packet,vertex,offset,3u).xyz;
}
float4 MeteorCluster4(ByteAddressBuffer data,uint packet,uint vertex,uint offset)
{
    return MeteorClusterChannels(data,packet,vertex,offset,4u);
}
LXSurfacePoint MeteorGeometryPoint(StructuredBuffer<float4> pose, ByteAddressBuffer topology, uint vertex)
{
    if (topology.Load(0u) < 5u || topology.Load(80u) == 0u)
    {
        return MeteorCompatibilityPoint(pose,vertex);
    }
    const uint2 map = topology.Load2(topology.Load(80u)+vertex*8u);
    LXSurfacePoint point = (LXSurfacePoint)0;
    if (map.x == 0u)
    {
        return point; // Only fully resident groups may reach a geometry consumer.
    }
    const uint mask = topology.Load(map.x+8u);
    float3 position = MeteorCluster3(topology,map.x,map.y,0u);
    const float3 normal = MeteorCluster3(topology,map.x,map.y,12u);
    const float2 uv = asfloat(uint2(MeteorClusterWord(topology,map.x,map.y,24u),MeteorClusterWord(topology,map.x,map.y,28u)));
    const uint tangentOffset = 32u + ((mask & 8u) != 0u ? 8u : 0u);
    const float4 tangent = MeteorCluster4(topology,map.x,map.y,tangentOffset);
    const float3 bitangent = cross(normal,tangent.xyz)*tangent.w;
    float3x3 transform = float3x3(1,0,0,0,1,0,0,0,1);
    const uint boneCount = MeteorPoseWord(pose,0u);
    if (boneCount != 0u)
    {
        const uint indicesOffset = tangentOffset+16u+((mask&32u)!=0u ? 16u : 0u);
        float4 weights = MeteorCluster4(topology,map.x,map.y,indicesOffset+4u);
        const uint packed = MeteorClusterWord(topology,map.x,map.y,indicesOffset);
        const uint4 indices = (packed >> uint4(0u,8u,16u,24u)) & 255u;
        const float sum = dot(weights,float4(1,1,1,1));
        if (sum > 0.0)
        {
            weights /= sum;
            float4x4 skin = (float4x4)0;
            for (uint influence=0u; influence<4u; ++influence)
            {
                if (weights[influence] > 0.0 && indices[influence] < boneCount)
                {
                    skin += weights[influence] * MeteorBone(pose,indices[influence]);
                }
            }
            position = mul(float4(position,1),skin).xyz;
            transform = (float3x3)skin;
        }
    }
    point.position = float4(position,0);
    point.uvLod = float4(uv,0,0);
    float3 n,t,b;
    TransformPbrFrame(normal,tangent.xyz,bitangent,transform,n,t,b);
    point.normal=float4(n,0);point.tangent=float4(t,0);point.bitangent=float4(b,0);
    return point;
}
float3 MeteorGeometryPosition(StructuredBuffer<float4> pose, ByteAddressBuffer topology, uint vertex)
{
    if (topology.Load(0u) < 5u || topology.Load(80u) == 0u)
    {
        return pose[vertex * 5u + 1u].xyz;
    }
    const uint2 map = topology.Load2(topology.Load(80u) + vertex * 8u);
    if (map.x == 0u)
    {
        return float3(0.0f);
    }
    float3 position = MeteorCluster3(topology, map.x, map.y, 0u);
    const uint boneCount = MeteorPoseWord(pose, 0u);
    if (boneCount != 0u)
    {
        const uint mask = topology.Load(map.x + 8u);
        const uint indicesOffset = 48u + ((mask & 8u) != 0u ? 8u : 0u) + ((mask & 32u) != 0u ? 16u : 0u);
        float4 weights = MeteorCluster4(topology, map.x, map.y, indicesOffset + 4u);
        const uint packed = MeteorClusterWord(topology, map.x, map.y, indicesOffset);
        const uint4 indices = (packed >> uint4(0u, 8u, 16u, 24u)) & 255u;
        const float sum = dot(weights, float4(1, 1, 1, 1));
        if (sum > 0.0f)
        {
            // 위치의 수치 결과를 유지하도록 기존 surface 경로와 같은 순서로
            // 가중치 정규화와 행렬 혼합을 수행한다.
            weights /= sum;
            float4x4 skin = (float4x4)0;
            for (uint influence = 0u; influence < 4u; ++influence)
            {
                if (weights[influence] > 0.0f && indices[influence] < boneCount)
                {
                    skin += weights[influence] * MeteorBone(pose, indices[influence]);
                }
            }
            position = mul(float4(position, 1), skin).xyz;
        }
    }
    return position;
}
bool MeteorClusterResident(ByteAddressBuffer topology, uint meshlet)
{
    return topology.Load(0u)<5u || topology.Load(84u)==0u || topology.Load(topology.Load(84u)+meshlet*4u)!=0u;
}
bool MeteorGroupResident(ByteAddressBuffer topology,uint group)
{
    return topology.Load(0u)<5u || topology.Load(88u)==0u || topology.Load(topology.Load(88u)+group*4u)!=0u;
}
