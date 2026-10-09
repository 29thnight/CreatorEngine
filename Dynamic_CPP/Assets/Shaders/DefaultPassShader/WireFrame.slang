#include "Includes/PackedBones.slang"

cbuffer WireFrameFrame : register(b0)
{
    float4x4 gViewProjection;
};

struct WireInstance
{
    float4x4 world;
    uint     boneOffset;
    uint3    padding;
};

StructuredBuffer<WireInstance> gInstances : register(t0);
StructuredBuffer<PackedBone>     gBones     : register(t1);

#define NO_SKINNING 0xFFFFFFFFu

struct VSIn
{
    float3 position    : POSITION;
    float4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VSOut
{
    float4 position : SV_POSITION;
};

VSOut TransformWire(float3 inputPosition, float4 boneIndices, float4 boneWeights, uint instanceId)
{
    const WireInstance instance = gInstances[instanceId];

    float3 position = inputPosition;
    if (instance.boneOffset != NO_SKINNING && boneWeights[0] > 0.0f)
    {
        float4x4 boneTransform = boneWeights[0]
            * UnpackBone(gBones[instance.boneOffset + (uint)boneIndices[0]]);

        [unroll]
        for (int i = 1; i < 4; ++i)
        {
            // Unused product influences carry index 255. Do not read a bone
            // outside this palette merely to multiply it by a zero weight.
            [branch]
            if (boneWeights[i] > 0.0f)
            {
                boneTransform += boneWeights[i]
                    * UnpackBone(gBones[instance.boneOffset + (uint)boneIndices[i]]);
            }
        }

        position = mul(float4(position, 1.0f), boneTransform).xyz;
    }

    const float4 worldPosition = mul(float4(position, 1.0f), instance.world);

    VSOut output;
    output.position = mul(worldPosition, gViewProjection);
    return output;
}

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    return TransformWire(input.position, input.boneIndices, input.boneWeights, instanceId);
}

struct ModelWireInput
{
    float3 position : POSITION;
    uint4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

VSOut VSMainModel(ModelWireInput input, uint instanceId : SV_InstanceID)
{
    return TransformWire(input.position, float4(input.boneIndices), input.boneWeights, instanceId);
}

VSOut VSMainStatic(float3 position : POSITION, uint instanceId : SV_InstanceID)
{
    return TransformWire(position, 0, 0, instanceId);
}

float4 PSMain() : SV_TARGET
{
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}
