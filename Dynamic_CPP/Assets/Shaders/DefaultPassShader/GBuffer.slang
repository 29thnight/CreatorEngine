#include "Includes/MaterialCoordinates.slang"
#include "Includes/MaterialEvaluation.slang"
#include "Includes/PbrCoverage.slang"

// GBuffer 기록 패스. GBuffer.hlsl 의 이식본이다.
//
// Frame/instance/bone bindings use b0, b2, t4, t5; material textures start at
// t16. Reflection determines the material table length. MRT formats are retained.
//
// ── HLSL 판과 달라진 것 ─────────────────────────────────────────────
//
//   · 접선 프레임이 Includes/MaterialEvaluation.slang 으로 나갔고, **퇴화 탄젠트를
//     판정**한다. 예전에는 탄젠트가 0 이면 normalize(0) = NaN 이 그대로
//     GBuffer 로 나갔다.
//   · AO consumes independent aoMap.R and occlusionStrength.

cbuffer PerFrame : register(b0)
{
    float4x4 gViewProjection;
};

// M6-P1a 제품 Standard Material 숫자 property 정본. ShaderMeta reflection이
// 이 b2의 byte layout을 검증하고 BuildDrawPool이 batch별 소유 bytes를 만든다.
cbuffer MaterialProperties : register(b2)
{
    float4 baseColor;
    float  metallic;
    float  roughness;
    float  normalScale;
    float  occlusionStrength;
    float3 emissive;
    float  alphaCutoff;
    float  emissiveStrength;
    float3 emissionPadding;
};

// 드로우별 상수를 cbuffer가 아니라 구조화 버퍼로 넘긴다.
//
// 같은 메시·재질을 쓰는 드로우가 여럿이면 인스턴스로 묶어 한 번에 그린다.
// cbuffer로는 그럴 수 없다 — 드로우마다 다른 값을 넣어야 하므로 드로우도
// 그만큼 나뉜다. 인스턴스 인덱스로 읽으면 한 드로우로 끝난다.
struct InstanceData
{
    float4x4 world;
    float4   baseColorFactor;
    float    metallic;
    float    roughness;
    uint     useNormalMap;

    // 이 인스턴스가 쓸 본 팔레트의 시작 위치. 0xFFFFFFFF면 스키닝 없음.
    //
    // ★ 이것이 DX11과 갈리는 지점이다. DX11은 본 팔레트를 cbuffer에 담아
    //   애니메이터가 바뀔 때마다 버퍼를 다시 올리고 드로우를 끊는다 —
    //   애니메이터 N개면 최소 드로우 N개다. 여기서는 모든 팔레트를 한
    //   버퍼에 이어 붙이고 인스턴스가 자기 오프셋을 들고 있으므로,
    //   애니메이터가 달라도 같은 (메시·재질) 배치에 남는다.
    uint     boneOffset;
    uint coverageFlags;
    float coverageCutoff;
    uint usePropertyBlock;
    uint padding;
};

StructuredBuffer<InstanceData> gInstances : register(t4);

// 프레임의 모든 본 팔레트를 이어 붙인 것. 인스턴스의 boneOffset이 자기
// 구간의 시작을 가리킨다.
StructuredBuffer<float4x4> gBones : register(t5);

static const uint kNoSkinning = 0xFFFFFFFFu;

// M6-P1b2a: 이름은 StandardMaterialProperty의 논리 property 정본과 같다.
// .shadermeta reflection이 이 이름을 t16..t20에 결합하며 C++ 배열 순서로 추측하지 않는다.
Texture2D    baseColorMap : register(t16);
Texture2D    normalMap    : register(t17);
Texture2D    ormMap       : register(t18);
Texture2D    emissiveMap  : register(t19);
Texture2D aoMap : register(t20);
SamplerState gSampler     : register(s0);

// M6-P1b2b1 제품 permutation 축. 0(full)이 기존 출력 정본이고 1(reduced)은
// normal-map 결과를 vertex normal 쪽으로 절반 완화한다. 두 변형 모두 같은
// resource/layout을 소비하므로 material별 PSO 전환만 독립적으로 검증할 수 있다.
#ifndef SHADING_QUALITY
#define SHADING_QUALITY 0
#endif

// MBC6 ModelAssetGeneration 레이아웃. MODEL_VERTEX_LAYOUT은 packed core,
// COLOR/SKINNING 축은 같은 vertexAttributeMask 기술표에서만 켜진다.
#if defined(MODEL_VERTEX_LAYOUT)
struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
#ifdef MODEL_VERTEX_UV1
    float2 uv1 : TEXCOORD1;
#endif
    float4 tangent  : TANGENT;
#ifdef MODEL_VERTEX_COLOR
    float4 color       : COLOR0;
#endif
#ifdef MODEL_VERTEX_SKINNING
    uint4  boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
#endif
};
#else
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
#endif

struct VSOut
{
    float2 uv1 : TEXCOORD6;
    float4 position  : SV_POSITION;
    nointerpolation uint coverageFlags : TEXCOORD2;
    nointerpolation float coverageCutoff : TEXCOORD3;
    nointerpolation uint usePropertyBlock : TEXCOORD4;
    float vertexAlpha : TEXCOORD5;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD0;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;

    // 픽셀 셰이더는 SV_InstanceID를 받을 수 없다. 재질 값은 정점에서 실어
    // 보내되 보간하지 않는다 — 인스턴스 안에서 상수이기 때문이다.
    nointerpolation float4 baseColorFactor : COLOR0;
    nointerpolation float3 material        : TEXCOORD1;  // x 금속성 · y 거칠기 · z 노멀맵
};

[shader("vertex")]
VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const InstanceData instance = gInstances[instanceId];
    const float4x4 gWorld = instance.world;

#ifdef MODEL_VERTEX_LAYOUT
    const float3 localTangent = input.tangent.xyz;
    const float3 localBitangent = cross(input.normal, localTangent) * input.tangent.w;
#else
    const float3 localTangent = input.tangent;
    const float3 localBitangent = input.bitangent;
#endif
    float3x3 frameTransform = (float3x3)gWorld;
#if !defined(MODEL_VERTEX_LAYOUT) || defined(MODEL_VERTEX_SKINNING)
    if (instance.boneOffset != kNoSkinning && input.boneWeights[0] > 0.0)
    {
        float4x4 boneTransform = input.boneWeights[0]
            * gBones[instance.boneOffset + uint(input.boneIndices[0])];
        [unroll]
        for (int i = 1; i < 4; ++i)
            boneTransform += input.boneWeights[i]
                * gBones[instance.boneOffset + uint(input.boneIndices[i])];
        input.position = mul(float4(input.position, 1.0), boneTransform).xyz;
        frameTransform = mul((float3x3)boneTransform, frameTransform);
    }
#endif

    VSOut output;
#ifdef MODEL_VERTEX_UV1
    output.uv1 = input.uv1;
#else
    output.uv1 = input.uv;
#endif
    output.coverageFlags = instance.coverageFlags;
    output.coverageCutoff = instance.coverageCutoff;
    output.usePropertyBlock = instance.usePropertyBlock;
#ifdef MODEL_VERTEX_COLOR
    output.vertexAlpha = input.color.a;
#else
    output.vertexAlpha = 1.0;
#endif
    const float4 worldPosition = mul(float4(input.position, 1.0), gWorld);
    output.position = mul(worldPosition, gViewProjection);

#ifdef MODEL_VERTEX_COLOR
    output.baseColorFactor = instance.baseColorFactor * input.color;
#else
    output.baseColorFactor = instance.baseColorFactor;
#endif
    output.material = float3(instance.metallic, instance.roughness,
        float(instance.useNormalMap));

    TransformPbrFrame(input.normal, localTangent, localBitangent, frameTransform,
        output.normal, output.tangent, output.bitangent);
    output.uv        = input.uv;
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

[shader("fragment")]
PSOut PSMain(VSOut input, bool frontFace : SV_IsFrontFace)
{
    const bool usePropertyBlock = input.usePropertyBlock != 0u;
    const float4 materialBaseColor = usePropertyBlock
        ? baseColor : input.baseColorFactor;
    const float materialMetallic = usePropertyBlock
        ? metallic : input.material.x;
    const float materialRoughness = usePropertyBlock
        ? roughness : input.material.y;
    const float materialNormalScale = usePropertyBlock ? normalScale : 1.0;

    const float3 materialEmissive = usePropertyBlock
        ? emissive : float3(1.0, 1.0, 1.0);

    PbrMaterial material = EvaluatePbrMaterial(
        baseColorMap.Sample(gSampler, MaterialUv(16, input.uv, input.uv1)),
        ormMap.Sample(gSampler, MaterialUv(18, input.uv, input.uv1)).rgb,
        emissiveMap.Sample(gSampler, MaterialUv(19, input.uv, input.uv1)).rgb,
        materialBaseColor, materialRoughness, materialMetallic, materialEmissive,
        usePropertyBlock ? emissiveStrength : 1.0);

    const float authoredAlpha = material.albedo.a
        * (usePropertyBlock ? input.vertexAlpha : 1.0);
    material.albedo.a = ApplyPbrCoverage(authoredAlpha,
        usePropertyBlock ? alphaCutoff : input.coverageCutoff, input.coverageFlags, frontFace);

    // metallicRoughness.R has no AO meaning unless also authored as aoMap.
    const float occlusion = EvaluatePbrOcclusion(aoMap.Sample(gSampler, MaterialUv(20, input.uv, input.uv1)).r,
        usePropertyBlock ? occlusionStrength : 1.0);
    const bool useNormalMap = input.material.z != 0.0;
    float3 normalSample = float3(0.5, 0.5, 1.0);
    if (useNormalMap) normalSample = normalMap.Sample(gSampler, MaterialUv(17, input.uv, input.uv1)).rgb;
    float3 normal = EvaluatePbrNormal(input.normal, input.tangent,
        input.bitangent, normalSample, materialNormalScale, useNormalMap,
        SHADING_QUALITY == 1);

    normal = OrientPbrNormal(normal, input.coverageFlags, frontFace);
    PSOut output;
    output.diffuse    = material.albedo;
    output.metalRough = float4(
        occlusion,
        material.roughness,
        material.metallic,
        1.0);
    output.normal     = float4(normal * 0.5 + 0.5, 1.0);
    output.emissive   = float4(
        material.emissive, 1.0);
    output.bitmask    = 0xABCDu;
    return output;
}
