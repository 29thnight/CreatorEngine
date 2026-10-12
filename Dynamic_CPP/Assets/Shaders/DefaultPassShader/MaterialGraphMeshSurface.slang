#include "Includes/MaterialGraphSurfacePoint.slang"
#include "Includes/MaterialEvaluation.slang"
#include "Includes/PackedBones.slang"

struct LXTransformJob
{
    row_major float4x4 world;
    uint4 countStrideMaskBones;
    uint4 offsets0;
    uint4 offsets1;
    uint4 inputOffsets;
};
cbuffer LXMeshConstants : register(b0) { uint4 lx_meshDispatch; };
ByteAddressBuffer lx_meshInputs : register(t0);
StructuredBuffer<LXTransformJob> lx_meshJobs : register(t1);
StructuredBuffer<uint2> lx_meshGroups : register(t2);
RWStructuredBuffer<LXSurfacePoint> lx_meshOutputs[4096] : register(u0);

PackedBone LXLoadTransformBone(uint base, uint index)
{
    PackedBone bone;
    base += index * 48;
    bone.row0 = asfloat(lx_meshInputs.Load4(base));
    bone.row1 = asfloat(lx_meshInputs.Load4(base + 16));
    bone.row2 = asfloat(lx_meshInputs.Load4(base + 32));
    return bone;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void LXTransformMesh(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID)
{
    const uint groupIndex = groupId.x + groupId.y * lx_meshDispatch.y;
    if (groupIndex >= lx_meshDispatch.x) return;
    const uint2 group = lx_meshGroups[groupIndex];
    if (group.x >= lx_meshDispatch.z) return;
    const LXTransformJob job = lx_meshJobs[group.x];
    const uint vertex = group.y + threadId.x;
    if (vertex >= job.countStrideMaskBones.x) return;
    const uint base = job.inputOffsets.x + vertex * job.countStrideMaskBones.y;
    float3 position = asfloat(lx_meshInputs.Load3(base + job.offsets0.x));
    const float3 normal = asfloat(lx_meshInputs.Load3(base + job.offsets0.y));
    const float2 uv = asfloat(lx_meshInputs.Load2(base + job.offsets0.z));
    const float4 tangent = asfloat(lx_meshInputs.Load4(base + job.offsets1.x));
    const float3 bitangent = cross(normal, tangent.xyz) * tangent.w;
    float3x3 frameTransform = (float3x3)job.world;
    if (job.countStrideMaskBones.w != 0)
    {
        const float4 weights = asfloat(lx_meshInputs.Load4(base + job.offsets1.w));
        if (weights.x > 0.0)
        {
            const uint packedIndices = lx_meshInputs.Load(base + job.offsets1.z);
            const uint4 indices = (packedIndices >> uint4(0, 8, 16, 24)) & 255u;
            float4x4 skin = weights.x * UnpackBone(LXLoadTransformBone(job.inputOffsets.y, indices.x));
            [unroll]
            for (uint i = 1; i < 4; ++i)
            {
                // Unused product influences carry the sentinel index 255.
                [branch]
                if (weights[i] > 0.0)
                {
                    skin += weights[i] * UnpackBone(LXLoadTransformBone(job.inputOffsets.y, indices[i]));
                }
            }
            position = mul(float4(position, 1), skin).xyz;
            frameTransform = mul((float3x3)skin, frameTransform);
        }
    }
    LXSurfacePoint result;
    result.uvLod = float4(uv, 0, asfloat(lx_meshInputs.Load(job.inputOffsets.z + vertex * 4)));
    result.position = float4(mul(float4(position, 1), job.world).xyz, 0);
    float3 n, t, b;
    TransformPbrFrame(normal, tangent.xyz, bitangent, frameTransform, n, t, b);
    result.normal = float4(n, 0);
    result.tangent = float4(t, 0);
    result.bitangent = float4(b, 0);
    lx_meshOutputs[NonUniformResourceIndex(group.x)][vertex] = result;
}

struct LXMeshSample
{
    uint triangle;
    float b;
    float c;
    float lod;
};
cbuffer LXSampleConstants : register(b2)
{
    uint4 lx_sampleCount;
};
StructuredBuffer<LXSurfacePoint> lx_sampleVertices : register(t4);
StructuredBuffer<uint> lx_sampleIndices : register(t5);
StructuredBuffer<LXMeshSample> lx_sampleRequests : register(t6);
RWStructuredBuffer<LXSurfacePoint> lx_sampleOutput : register(u1);

[shader("compute")]
[numthreads(32, 1, 1)]
void LXSampleMesh(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_sampleCount.x)
    {
        return;
    }
    const LXMeshSample sample = lx_sampleRequests[id.x];
    const uint offset = sample.triangle * 3;
    const LXSurfacePoint a = lx_sampleVertices[lx_sampleIndices[offset]];
    const LXSurfacePoint b = lx_sampleVertices[lx_sampleIndices[offset + 1]];
    const LXSurfacePoint c = lx_sampleVertices[lx_sampleIndices[offset + 2]];
    // Use the same rounded sum as the host's B + C <= 1 acceptance check.
    // Sequential subtraction can produce a negative A on an accepted edge.
    const float wa = 1.0 - (sample.b + sample.c);
    LXSurfacePoint result;
    result.uvLod = a.uvLod * wa + b.uvLod * sample.b + c.uvLod * sample.c;
    result.uvLod.w = sample.lod;
    result.position = a.position * wa + b.position * sample.b + c.position * sample.c;
    result.normal = a.normal * wa + b.normal * sample.b + c.normal * sample.c;
    result.tangent = a.tangent * wa + b.tangent * sample.b + c.tangent * sample.c;
    result.bitangent = a.bitangent * wa + b.bitangent * sample.b + c.bitangent * sample.c;
    lx_sampleOutput[id.x] = result;
}

// Geometry diagnostic host: real indexed triangles, with the same GPU world
// frame consumed by material evaluation. No IBL interpolation claim is made.
StructuredBuffer<LXSurfacePoint> lx_meshDrawPoints : register(t3);
cbuffer LXMeshDrawConstants : register(b1)
{
    row_major float4x4 lx_meshViewProjection;
};
struct LXMeshVarying
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
};
[shader("vertex")]
LXMeshVarying LXMeshVS(uint id : SV_VertexID)
{
    LXMeshVarying result;
    result.world = lx_meshDrawPoints[id].position.xyz;
    result.position = mul(float4(result.world, 1), lx_meshViewProjection);
    return result;
}
[shader("fragment")]
float4 LXMeshPS(LXMeshVarying input) : SV_Target
{
    return float4(input.world, 1);
}
