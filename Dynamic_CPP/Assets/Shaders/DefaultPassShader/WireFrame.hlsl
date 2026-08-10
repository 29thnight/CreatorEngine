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
StructuredBuffer<float4x4>     gBones     : register(t1);

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

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const WireInstance instance = gInstances[instanceId];

    float3 position = input.position;
    if (instance.boneOffset != NO_SKINNING && input.boneWeights[0] > 0.0f)
    {
        float4x4 boneTransform = input.boneWeights[0]
            * gBones[instance.boneOffset + (uint)input.boneIndices[0]];

        [unroll]
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += input.boneWeights[i]
                * gBones[instance.boneOffset + (uint)input.boneIndices[i]];
        }

        position = mul(float4(position, 1.0f), boneTransform).xyz;
    }

    const float4 worldPosition = mul(float4(position, 1.0f), instance.world);

    VSOut output;
    output.position = mul(worldPosition, gViewProjection);
    return output;
}

float4 PSMain() : SV_TARGET
{
    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}
