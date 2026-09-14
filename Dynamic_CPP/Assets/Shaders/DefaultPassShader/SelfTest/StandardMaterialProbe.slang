cbuffer PerFrame : register(b0)
{
    float4x4 gViewProjection;
};

// M6-P0의 한 재질 수직 프로브다. 저장 정본인 Material property block이
// ShaderMeta reflection의 b2 layout을 따라 실제 픽셀까지 도달하는지만 본다.
// texture와 다중 material batching은 이 프로브가 초록인 뒤의 별도 절편이다.
cbuffer MaterialProperties : register(b2)
{
    float4 baseColor;
    float  metallic;
    float  roughness;
    float  normalScale;
    float  occlusionStrength;
    float3 emissive;
    float  alphaCutoff;
};

struct InstanceData
{
    float4x4 world;
    float4   legacyBaseColorFactor;
    float    legacyMetallic;
    float    legacyRoughness;
    uint     legacyUseNormalMap;
    uint     boneOffset;
};

StructuredBuffer<InstanceData> gInstances : register(t4);

struct VSIn
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float2 uv          : TEXCOORD0;
    float3 tangent     : TANGENT;
    float3 bitangent   : BINORMAL;
    float4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
};

struct VSOut
{
    float4 position : SV_POSITION;
};

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    VSOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0f),
        gInstances[instanceId].world);
    output.position = mul(worldPosition, gViewProjection);

    // GBuffer PSO의 입력 기술표 전체를 실제로 소비한다. 읽지 않은 attribute를
    // 그대로 두면 Vulkan validation이 "layout에는 있으나 shader가 안 읽는다"를
    // 보고한다. 화면을 바꾸지 않는 1e-7 clip offset에 모두 참여시켜, fixture가
    // 실제 제품 입력 계약과 어긋났을 때 경고 없이 숨지 못하게 한다.
    const float inputContract =
        dot(input.normal, float3(1.0f, 2.0f, 3.0f)) +
        dot(input.uv, float2(5.0f, 7.0f)) +
        dot(input.tangent, float3(11.0f, 13.0f, 17.0f)) +
        dot(input.bitangent, float3(19.0f, 23.0f, 29.0f)) +
        dot(input.boneIndices, float4(31.0f, 37.0f, 41.0f, 43.0f)) +
        dot(input.boneWeights, float4(47.0f, 53.0f, 59.0f, 61.0f));
    output.position.xy += inputContract * 1.0e-7f;
    return output;
}

struct PSOut
{
    float4 diffuse    : SV_TARGET0;
    float4 metalRough : SV_TARGET1;
    float4 normal     : SV_TARGET2;
    float4 emissive   : SV_TARGET3;
    uint   bitmask    : SV_TARGET4;
};

PSOut PSMain(VSOut input)
{
    PSOut output;
    output.diffuse = baseColor;
    output.metalRough = float4(
        occlusionStrength, roughness, metallic, alphaCutoff);
    output.normal = float4(normalScale, 0.0f, 0.0f, 1.0f);
    output.emissive = float4(emissive, 1.0f);
    output.bitmask = 0x4D36u; // "M6"
    return output;
}
