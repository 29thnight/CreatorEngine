#include "Includes/TextureCoordinates.slang"
#include "Includes/PbrCoverage.slang"

cbuffer ShadowConstants : register(b0)
{
    float4x4 gLightViewProjection;
};

// World, skin and material coverage are immutable per-instance inputs.
struct ShadowInstance
{
    float4x4 world;
    uint     boneOffset;
    uint coverageFlags;
    float cutoff;
    float baseAlpha;
    TextureCoordinates coordinates;
};

StructuredBuffer<ShadowInstance> gInstances : register(t0);

#ifdef SHADOW_SKINNING
StructuredBuffer<float4x4> gBones : register(t1);
#define NO_SKINNING 0xFFFFFFFFu
#endif

Texture2D gBaseColor : register(t2);
SamplerState gSampler : register(s0);

struct VSIn
{
    // Keep declaration order equal to the consumed vertex layout: SPIR-V
    // locations follow this order while DXIL uses the semantic names.
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
#ifdef MODEL_VERTEX_UV1
    float2 uv1 : TEXCOORD1;
#endif
#ifdef MODEL_VERTEX_COLOR
    float4 color : COLOR0;
#endif
#ifdef SHADOW_SKINNING
#ifdef MODEL_VERTEX_SKINNING
    uint4  boneIndices : BLENDINDICES;
#else
    float4 boneIndices : BLENDINDICES;
#endif
    float4 boneWeights : BLENDWEIGHT;
#endif
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint flags : TEXCOORD1;
    nointerpolation float cutoff : TEXCOORD2;
    float alpha : TEXCOORD3;
};

[shader("vertex")]
VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const ShadowInstance instance = gInstances[instanceId];

#ifdef SHADOW_SKINNING
    // GBuffer와 같은 수식·같은 규약이다. 둘이 갈리면 캐릭터의 그림자가 몸과
    // 다른 포즈로 나오는데, 그 증상은 '그림자가 좀 이상하다'로만 보인다.
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

        input.position = mul(float4(input.position, 1.0f), boneTransform).xyz;
    }
#endif

    const float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);
    VSOut output;
    output.position = mul(worldPosition, gLightViewProjection);
#ifdef MODEL_VERTEX_UV1
    output.uv = TransformTextureUv(instance.coordinates, input.uv, input.uv1);
#else
    output.uv = TransformTextureUv(instance.coordinates, input.uv, input.uv);
#endif
    output.flags = instance.coverageFlags;
    output.cutoff = instance.cutoff;
    output.alpha = instance.baseAlpha;
#ifdef MODEL_VERTEX_COLOR
    output.alpha *= input.color.a;
#endif
    return output;
}

[shader("fragment")]
void PSMain(VSOut input, bool frontFace : SV_IsFrontFace)
{
    float alpha = input.alpha;
    if ((input.flags & PBR_COVERAGE_MASKED) != 0u)
        alpha *= gBaseColor.Sample(gSampler, input.uv).a;
    ApplyPbrCoverage(alpha, input.cutoff, input.flags, frontFace);
}
