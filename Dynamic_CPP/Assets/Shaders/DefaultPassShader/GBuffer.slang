#include "Includes/TangentFrame.slang"

// GBuffer 기록 패스. GBuffer.hlsl 의 이식본이다.
//
// ★ 바인딩(b0, b2, t0..t3, t4, t5, s0)·정점 레이아웃·MRT 5장은 한 바이트도
//   바뀌지 않았다 — ShaderMeta reflection 과 BuildDrawPool 이 그대로 쓴다.
//
// ── HLSL 판과 달라진 것 ─────────────────────────────────────────────
//
//   · 접선 프레임이 Includes/TangentFrame.slang 으로 나갔고, **퇴화 탄젠트를
//     판정**한다. 예전에는 탄젠트가 0 이면 normalize(0) = NaN 이 그대로
//     GBuffer 로 나갔다.
//   · occlusion 이 중립 1 로 고정됐다. 예전의 `orm.r * strength` 는 규격
//     공식도 아니었고, 무엇보다 그 R 은 metallicRoughness 텍스처의 **규격상
//     미정의 채널**이다 — PSMain 안의 근거와 aoMap 슬롯 계획 참조.

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
};

StructuredBuffer<InstanceData> gInstances : register(t4);

// 프레임의 모든 본 팔레트를 이어 붙인 것. 인스턴스의 boneOffset이 자기
// 구간의 시작을 가리킨다.
StructuredBuffer<float4x4> gBones : register(t5);

static const uint kNoSkinning = 0xFFFFFFFFu;

// M6-P1b2a: 이름은 StandardMaterialProperty의 논리 property 정본과 같다.
// .shadermeta reflection이 이 이름을 t0..t3에 결합하며 C++ 배열 순서로 추측하지 않는다.
Texture2D    baseColorMap : register(t0);
Texture2D    normalMap    : register(t1);
Texture2D    ormMap       : register(t2);
Texture2D    emissiveMap  : register(t3);
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
    float4 position  : SV_POSITION;
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

#if defined(MODEL_VERTEX_LAYOUT)
#ifdef MODEL_VERTEX_SKINNING
    if (instance.boneOffset != kNoSkinning && input.boneWeights[0] > 0.0)
    {
        float4x4 boneTransform = input.boneWeights[0]
            * gBones[instance.boneOffset + uint(input.boneIndices[0])];

        [unroll]
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += input.boneWeights[i]
                * gBones[instance.boneOffset + uint(input.boneIndices[i])];
        }

        input.position    = mul(float4(input.position, 1.0), boneTransform).xyz;
        input.normal      = normalize(mul(float4(input.normal, 0.0), boneTransform).xyz);
        input.tangent.xyz = normalize(mul(float4(input.tangent.xyz, 0.0), boneTransform).xyz);
    }
#endif
    const float3 localTangent   = input.tangent.xyz;
    const float3 localBitangent =
        cross(input.normal, input.tangent.xyz) * input.tangent.w;
#else
    // ── 스키닝 ──
    //
    // DX11 VertexShader.vs의 이식이다. 판정 조건(boneWeight[0] > 0)과 네 본의
    // 가중 합, 법선·탄젠트에도 같은 변환을 적용하는 것까지 같다.
    //
    // 규약만 다르다: DX11은 mul(bone, v)(열 벡터), 여기는 mul(v, bone)(행
    // 벡터)이다. CPU가 전치해서 올리므로 결과 행렬은 같다 — world 행렬이
    // 이미 그 규약으로 오고 있고, 본만 다른 규약을 쓰면 팔·다리가 엉뚱한
    // 곳으로 날아간다.
    if (instance.boneOffset != kNoSkinning && input.boneWeights[0] > 0.0)
    {
        float4x4 boneTransform = input.boneWeights[0]
            * gBones[instance.boneOffset + uint(input.boneIndices[0])];

        [unroll]
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += input.boneWeights[i]
                * gBones[instance.boneOffset + uint(input.boneIndices[i])];
        }

        input.position  = mul(float4(input.position, 1.0), boneTransform).xyz;
        input.normal    = normalize(mul(float4(input.normal, 0.0), boneTransform).xyz);
        input.tangent   = normalize(mul(float4(input.tangent, 0.0), boneTransform).xyz);
        input.bitangent = normalize(mul(float4(input.bitangent, 0.0), boneTransform).xyz);
    }
    const float3 localTangent   = input.tangent;
    const float3 localBitangent = input.bitangent;
#endif

    VSOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0), gWorld);
    output.position = mul(worldPosition, gViewProjection);

#ifdef MODEL_VERTEX_COLOR
    output.baseColorFactor = instance.baseColorFactor * input.color;
#else
    output.baseColorFactor = instance.baseColorFactor;
#endif
    output.material = float3(instance.metallic, instance.roughness,
        float(instance.useNormalMap));

    // 법선·탄젠트는 회전만 적용한다. 비균등 스케일에는 역전치 행렬이 필요한데,
    // 그건 스케일이 실제로 문제가 되는 씬이 나왔을 때 상수에 추가한다 —
    // 지금 넣으면 검증할 수 없는 값이 하나 더 늘어난다.
    output.normal    = mul(input.normal,    (float3x3)gWorld);
    output.tangent   = mul(localTangent,    (float3x3)gWorld);
    output.bitangent = mul(localBitangent,  (float3x3)gWorld);
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
PSOut PSMain(VSOut input)
{
    // alphaCutoff < 0은 snapshot이 없는 기존 격리 fixture의 호환 sentinel이다.
    // 제품 BuildDrawPool은 항상 0 이상인 reflection-packed property를 싣는다.
    const bool usePropertyBlock = alphaCutoff >= 0.0;
    const float4 materialBaseColor = usePropertyBlock
        ? baseColor : input.baseColorFactor;
    const float materialMetallic = usePropertyBlock
        ? metallic : input.material.x;
    const float materialRoughness = usePropertyBlock
        ? roughness : input.material.y;
    const float materialNormalScale = usePropertyBlock ? normalScale : 1.0;

    const float3 materialEmissive = usePropertyBlock
        ? emissive : float3(1.0, 1.0, 1.0);

    // 텍스처가 없는 슬롯에는 슬롯 의미에 맞는 1x1 폴백이 묶여 있다(베이스는
    // 흰색, ORM은 중립 (1,1,0), emissive는 검정 — PrepareFrame 참조). 그래서
    // "텍스처가 있으면" 분기가 필요 없다 — 분기 없는 쪽이 셰이더에서 빠르고,
    // 재질마다 다른 셰이더 변형을 만들지 않아도 된다.
    const float4 albedo = baseColorMap.Sample(gSampler, input.uv) * materialBaseColor;

    // occlusion/roughness/metallic은 glTF 규약대로 G에 거칠기, B에 금속성이다.
    const float3 orm = ormMap.Sample(gSampler, input.uv).rgb;

    // ── occlusion ──
    //
    // ★ 지금은 중립 1 을 쓴다. 읽을 자리가 없어서다.
    //
    //   임포터는 glTF 의 metallicRoughness 텍스처를 ormMap 에 묶는데
    //   (SceneToModelDraft: metallicRoughnessMap → OrmMap), 규격에서 그
    //   텍스처의 R 은 **미정의**다. occlusionTexture 는 별도 semantic 이고
    //   이 셰이더에는 그것을 받을 aoMap 슬롯이 없다(t0..t3 이 전부다).
    //
    //   실측: Gunner·SU 의 _M 은 R 이 255 라 우연히 무해하지만 scene.glb 의
    //   MR 텍스처 20 장은 R 이 0~103 이고 그중 하나는 전 텍셀이 0 이다.
    //   여기서 orm.r 을 AO 로 소비하면 그 씬은 앰비언트가 통째로 죽는다.
    //   예전 코드의 `orm.r * occlusionStrength` 가 정확히 그 식이었다
    //   (게다가 그것은 glTF 공식 lerp(1, r, strength) 도 아니다).
    //
    //   ★ 임포터에서 "AO 가 저작됐을 때만 strength 를 살리는" 쪽으로 풀려다
    //     되돌렸다. occlusionStrength 는 재질 지문에 들어가므로(ModelStableKeys)
    //     값을 바꾸면 **무명 요소의 stable key 가 통째로 흔들린다** — scene.glb
    //     의 무명 재질 25 개가 identity 가드에 막혀 재임포트가 거부됐다.
    //     코퍼스에 occlusionTexture 를 가진 자산은 0 건이라, 저작 근거가 없는
    //     기능을 위해 지문 이주를 감수할 이유가 없다.
    //
    //   AO 를 실제로 켜려면 aoMap 슬롯 신설이 먼저다(.shadermeta 선언 +
    //   PrepareFrame 폴백 + 디스크립터 테이블). 그때 이 자리는 규격 공식
    //   lerp(1, aoMap.r, occlusionStrength) 가 된다.
    const float occlusion = 1.0;

    const float3 geometricNormal = normalize(input.normal);
    float3 normal = geometricNormal;
    if (input.material.z != 0.0)
    {
        const TangentFrame frame = BuildTangentFrame(input.normal,
            input.tangent, input.bitangent);

        const float3 sampled = normalMap.Sample(gSampler, input.uv).rgb * 2.0 - 1.0;
        normal = ApplyTangentSpaceNormal(frame, geometricNormal, sampled,
            materialNormalScale);
    }
#if SHADING_QUALITY == 1
    normal = normalize(normal + geometricNormal);
#endif

    PSOut output;
    output.diffuse    = albedo;
    output.metalRough = float4(
        occlusion,
        orm.g * materialRoughness,
        orm.b * materialMetallic,
        1.0);
    output.normal     = float4(normal * 0.5 + 0.5, 1.0);
    output.emissive   = float4(
        emissiveMap.Sample(gSampler, input.uv).rgb * materialEmissive, 1.0);
    output.bitmask    = 0xABCDu;
    return output;
}
