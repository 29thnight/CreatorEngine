cbuffer ShadowConstants : register(b0)
{
    float4x4 gLightViewProjection;
};

// 드로우마다 바뀌는 것은 월드 행렬과 본 오프셋뿐이다. GBuffer처럼 재질까지
// 실을 필요가 없다.
//
// 정적 경로도 같은 구조를 읽는다(boneOffset을 무시할 뿐). 구조를 갈라 두면
// 배치 코드가 두 벌이 되고 그쪽이 훨씬 비싸다.
struct ShadowInstance
{
    float4x4 world;
    uint     boneOffset;
    uint3    padding;
};

StructuredBuffer<ShadowInstance> gInstances : register(t0);

#ifdef SHADOW_SKINNING
StructuredBuffer<float4x4> gBones : register(t1);
#define NO_SKINNING 0xFFFFFFFFu
#endif

struct VSIn
{
    float3 position : POSITION;
#ifdef SHADOW_SKINNING
    // I5-D34b: experiment packed(68B)는 BLENDINDICES가 uint4다 — 본문의
    // (uint) 캐스트가 항등이 되므로 선언만 갈리면 수식은 한 벌이다.
#ifdef EXPERIMENT_SKINNED_VERTEX
    uint4  boneIndices : BLENDINDICES;
#else
    float4 boneIndices : BLENDINDICES;
#endif
    float4 boneWeights : BLENDWEIGHT;
#endif
};

// 깊이만 쓰므로 픽셀 셰이더가 없다. 위치 외에는 아무것도 보간하지 않는다 —
// 그림자 패스는 드로우 수가 많아 정점 처리 비용이 그대로 프레임에 얹힌다.
float4 VSMain(VSIn input, uint instanceId : SV_InstanceID) : SV_POSITION
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
    return mul(worldPosition, gLightViewProjection);
}
