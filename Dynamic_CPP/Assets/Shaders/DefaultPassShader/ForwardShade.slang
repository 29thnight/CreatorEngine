#include "Includes/MaterialCoordinates.slang"
#include "Includes/MaterialEvaluation.slang"
#include "Includes/PbrSurface.slang"
#include "Includes/PbrCoverage.slang"

// MBC6: ModelAssetGeneration의 전체 mask가 입력 레이아웃과 shader 축을 함께
// 만든다. Forward도 GBuffer/Shadow와 같은 palette 규약으로 skin pose를 계산한다.
#ifdef MODEL_VERTEX_LAYOUT
struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
#ifdef MODEL_VERTEX_UV1
    float2 uv1 : TEXCOORD1;
#endif
    float4 tangent  : TANGENT;
#ifdef MODEL_VERTEX_COLOR
    float4 color : COLOR0;
#endif
#ifdef MODEL_VERTEX_SKINNING
    uint4 boneIndices : BLENDINDICES;
    float4 boneWeights : BLENDWEIGHT;
#endif
};
#else
struct VSIn
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
};
#endif

struct VSOut
{
    float2 uv1 : TEXCOORD12;
    float4 position  : SV_POSITION;
    nointerpolation uint coverageFlags : TEXCOORD9;
    nointerpolation float coverageCutoff : TEXCOORD10;
    float vertexAlpha : TEXCOORD11;
    float3 worldPos  : TEXCOORD0;
    float3 normal    : TEXCOORD1;
    float2 uv        : TEXCOORD2;
    float3 tangent   : TEXCOORD5;
    float3 bitangent : TEXCOORD6;
    // 인스턴스마다 상수라 보간하지 않는다. 보간을 켜 두면 값은 같아도
    // 래스터라이저가 헛일을 한다.
    nointerpolation float4 baseColor : TEXCOORD3;
    // x 금속성 · y 거칠기 · z 노멀맵 · w 재질 텍스처가 묶였는가
    nointerpolation float4 material  : TEXCOORD4;
    nointerpolation float4 flowWind  : TEXCOORD7;
    nointerpolation float4 flowUvTime : TEXCOORD8;
};

struct ShadeInstance
{
    float4x4 world;
    float4   baseColor;
    float    metallic;
    float    roughness;
    uint     useNormalMap;
    // bit0: material texture table, bit1: owning ShaderMeta snapshot.
    uint     materialFlags;
    // P2d-b immutable Material::m_flowInfo + producer frame time.
    float4   flowWindVector;
    float2   flowUvScroll;
    float    flowTotalSeconds;
    float    flowDeltaSeconds;
    uint     boneOffset;
    uint coverageFlags;
    float coverageCutoff;
    uint bonePadding;
};

// M6-P2b Standard Forward material numeric contract. ShaderMeta reflection owns
// this b2/space0 layout. Snapshot 없는 legacy/self-test draw는 ShadeInstance의
// 명시적 flag로 per-instance 값을 계속 소비해 기존 ABI를 보존한다.
// The prefix matches the 48-byte Standard Material block used by GBuffer;
// representative Forward materials may append a reflected 16-byte numeric tail.
cbuffer MaterialProperties : register(b2)
{
    float4 baseColor;
    float  metallic;
    float  roughness;
    float  normalScale;
    float  occlusionStrength;
    float3 emissive;
    float  alphaCutoff;
#if defined(FORWARD_WATER_MATERIAL)
    float  waveSpeed;
    float  waveAmplitude;
    float  waveFrequency;
    float  waterTint;
#elif defined(FORWARD_WIND_MATERIAL)
    float  windSpeed;
    float  windStrength;
    float  windFrequency;
    float  windTint;
#endif
    // I5-M5 flow 승격 — wind/uvScroll 저작의 정본은 material CB(b2)다. 인스턴스
    // 채널(ShadeInstance)의 사본은 snapshot 없는 legacy/self-test draw 폴백으로만
    // 남는다. 시간(flowTotalSeconds/DeltaSeconds)은 프레임 값이라 계속 인스턴스로
    // 온다. float2 다음 float4는 16B 정렬된다. W6 강도와 padding을 뒤에
    // 덧붙여 기존 flow offset을 보존하고 전체 CB도 16B 정렬로 끝낸다.
    float2 flowUvScroll;
    float4 flowWindVector;
    float  emissiveStrength;
    float3 emissionPadding;
};

StructuredBuffer<ShadeInstance> gInstances : register(t0);
StructuredBuffer<float4>        gLights    : register(t1);
StructuredBuffer<uint>          gTileCount : register(t2);
StructuredBuffer<uint>          gTileList  : register(t3);
#ifdef MODEL_VERTEX_SKINNING
StructuredBuffer<float4x4>      gBones     : register(t12);
#define NO_SKINNING 0xFFFFFFFFu
#endif

// M6-P2d-c: 물리 슬롯은 t4..t7/space0으로 유지하되 texture property 이름은
// ShaderMeta/Slang reflection 정본이다. Wind fixture가 t4를 windMap으로 바꿔
// Standard 이름을 C++에서 하드코딩하지 않는 제품 경로를 판정한다.
#ifndef FORWARD_BASE_COLOR_TEXTURE
#define FORWARD_BASE_COLOR_TEXTURE baseColorMap
#endif
Texture2D FORWARD_BASE_COLOR_TEXTURE : register(t16);
Texture2D normalMap    : register(t17);
Texture2D ormMap       : register(t18);
Texture2D emissiveMap  : register(t19);
Texture2D aoMap : register(t20);

// Material-authored axis only. TILE_SIZE/MAX_LIGHTS_PER_TILE and
// REFERENCE_PATH are Forward pass system defines and intentionally stay out of
// Forward.shadermeta, so the host can compose them without duplicate axes.
#ifndef SHADING_QUALITY
#define SHADING_QUALITY 0
#endif

// IBL 셋(Deferred와 같은 split-sum). 없으면 앰비언트가 0이다.
TextureCube gIrradiance  : register(t8);
TextureCube gPrefiltered : register(t9);
Texture2D   gBrdfLut     : register(t10);

// 캐스케이드 그림자 맵. Deferred가 t5로 읽는 것과 같은 자원이다.
Texture2DArray gShadowMap : register(t11);

// 생성기(EnhancedIBLGenerator::kPrefilterMips)와 같아야 한다.
#define IBL_PREFILTER_MIPS 6

// EnhancedRenderPass.h의 kShadowCascadeCount와 같아야 한다.
#define SHADOW_CASCADE_COUNT 3

SamplerState gSampler    : register(s0);
// IBL용 선형 클램프. 거칠기가 밉 좌표라 포인트로 읽으면 단차가 띠로 보인다.
SamplerState gIblSampler : register(s1);
// 그림자 비교 샘플러. 하드웨어가 이웃 텍셀 넷을 비교·평균해 준다(2x2 PCF).
SamplerComparisonState gShadowSampler : register(s2);

cbuffer ShadeParams : register(b0)
{
    float4x4 gViewProjection;
    uint2    gTileGrid;
    uint     gLightCount;
    uint     gPad0;
    float4   gEyePosition;   // xyz — 시선 방향의 기준
    uint     gHasIbl;
    uint3    gPad1;

    // ── 그림자 (Deferred의 LightingConstants와 같은 뜻) ──
    float4x4 gLightViewProjection[SHADOW_CASCADE_COUNT];
    float4   gCameraForward;    // 뷰 깊이를 재는 축
    float4   gCascadeSplits;    // xyz = 각 캐스케이드가 끝나는 뷰 깊이
    float4   gShadowBias;       // xyz = 캐스케이드별 편향 · w = 경사 계수
    uint     gHasShadow;
    float    gCascadeBlendBand; // 경계 블렌딩 폭 · 0이면 하드 스위치
    uint2    gPad2;
};

// ── 캐스케이드 그림자 (EnhancedDeferredPass와 같은 식) ──
//
// 그림자 계산은 Deferred와 같은 캐스케이드 규약을 따른다.
// 같은 주의가 붙는다: 값이 갈리면 같은 자리에서 불투명은 그늘인데 투명만
// 밝은, 물체와 무관한 경계가 생긴다 — 고칠 때는 두 곳을 함께 고칠 것.
float SampleShadowCascade(uint cascade, float3 worldPosition, float slopeTan)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0f), gLightViewProjection[cascade]);
    const float3 projected = lightSpace.xyz / lightSpace.w;

    // 라이트 상자 밖은 그림자 정보가 없다. 가려진 것으로 치면 맵 경계에
    // 검은 띠가 생기므로 빛을 받는 것으로 둔다.
    if (any(abs(projected.xy) > 1.0f) || projected.z > 1.0f) return 1.0f;

    const float2 uv = float2(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f);
    const float bias = gShadowBias[cascade] * (1.0f + gShadowBias.w * slopeTan);

    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        float3(uv, (float)cascade), projected.z - bias);
}

// 방향광 캐스케이드 그림자. 반환값 1은 빛을 받음, 0은 가려짐.
float SampleShadow(float3 worldPosition, float3 normal, float3 toLight)
{
    if (gHasShadow == 0) return 1.0f;

    const float viewDepth = dot(worldPosition - gEyePosition.xyz, gCameraForward.xyz);

    uint cascade = 0;
    if (viewDepth > gCascadeSplits.x) cascade = 1;
    if (viewDepth > gCascadeSplits.y) cascade = 2;

    const float ndotl = saturate(dot(normal, toLight));
    const float slopeTan = min(sqrt(max(1.0f - ndotl * ndotl, 0.0f)) / max(ndotl, 1e-3f), 8.0f);

    float shadow = SampleShadowCascade(cascade, worldPosition, slopeTan);

    if (gCascadeBlendBand > 0.0f && cascade < SHADOW_CASCADE_COUNT - 1)
    {
        const float split = (cascade == 0) ? gCascadeSplits.x : gCascadeSplits.y;
        const float bandStart = split * (1.0f - gCascadeBlendBand);
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4f));
            shadow = lerp(shadow,
                SampleShadowCascade(cascade + 1, worldPosition, slopeTan), blend);
        }
    }

    return shadow;
}

[shader("vertex")]
VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const ShadeInstance instance = gInstances[instanceId];

#ifdef MODEL_VERTEX_LAYOUT
    const float3 localTangent = input.tangent.xyz;
    const float3 localBitangent = cross(input.normal, localTangent) * input.tangent.w;
#else
    const float3 localTangent = input.tangent;
    const float3 localBitangent = input.bitangent;
#endif
    float3x3 frameTransform = (float3x3)instance.world;

#ifdef MODEL_VERTEX_SKINNING
    if (instance.boneOffset != NO_SKINNING && input.boneWeights[0] > 0.0f)
    {
        float4x4 boneTransform = input.boneWeights[0]
            * gBones[instance.boneOffset + input.boneIndices[0]];
        [unroll]
        for (int i = 1; i < 4; ++i)
        {
            boneTransform += input.boneWeights[i]
                * gBones[instance.boneOffset + input.boneIndices[i]];
        }
        input.position = mul(float4(input.position, 1.0f), boneTransform).xyz;
        frameTransform = mul((float3x3)boneTransform, frameTransform);
    }
#endif

    const float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);

    VSOut output;
#ifdef MODEL_VERTEX_UV1
    output.uv1 = input.uv1;
#else
    output.uv1 = input.uv;
#endif
    output.coverageFlags = instance.coverageFlags;
    output.coverageCutoff = instance.coverageCutoff;
#ifdef MODEL_VERTEX_COLOR
    output.vertexAlpha = input.color.a;
#else
    output.vertexAlpha = 1.0;
#endif
    output.position  = mul(worldPosition, gViewProjection);
    output.worldPos  = worldPosition.xyz;
    output.uv        = input.uv;
#ifdef MODEL_VERTEX_COLOR
    output.baseColor = instance.baseColor * input.color;
#else
    output.baseColor = instance.baseColor;
#endif
    output.material  = float4(instance.metallic, instance.roughness,
        (float)instance.useNormalMap, (float)instance.materialFlags);
    // flow 승격의 CB 선택은 PS에서 한다 — VS가 b2를 읽으면 root signature의
    // CBV 스테이지 가시성이 어긋난다(실측: PSO 생성 E_INVALIDARG). 인스턴스
    // 채널은 그대로 나르고 PS가 materialFlags로 고른다.
    output.flowWind = instance.flowWindVector;
    output.flowUvTime = float4(instance.flowUvScroll,
        instance.flowTotalSeconds, instance.flowDeltaSeconds);

    TransformPbrFrame(input.normal, localTangent, localBitangent, frameTransform,
        output.normal, output.tangent, output.bitangent);
    return output;
}

// Both light-list paths use the shared Deferred/Forward surface.
float3 Contribute(uint lightIndex, PbrSurface surface)
{
    const float4 position    = gLights[lightIndex * 4 + 0];
    const float4 direction   = gLights[lightIndex * 4 + 1];
    const float4 color       = gLights[lightIndex * 4 + 2];
    const float4 attenuation = gLights[lightIndex * 4 + 3];

    float3 toLight;
    float  falloff = 1.0f;

    if (position.w < 0.5f)
    {
        // 방향광 — Deferred와 같은 규약으로 direction 슬롯을 읽는다.
        //
        // ★ 예전에는 position을 방향으로 봤다("direction 슬롯은 여기서 안
        //   쓴다"고 적혀 있었다). 그런데 밀봉이 position에는 광원의 위치를,
        //   direction에는 방향을 따로 싣는다 — 해가 씬 위에 놓여 있으면
        //   -position이 아래를 가리켜 온 세상이 밑에서만 조명받는다.
        //   투명을 배선하고 나서야 눈에 보였다(그 전에는 소비자가 없었다).
        toLight = -normalize(direction.xyz);

        // 그림자는 방향광에만 붙는다 — Deferred가 같다(점광·스포트는
        // 그림자 맵 자체가 없다). gHasShadow가 0이면 1이 나오므로 그림자
        // 없는 경로(자가 검증)는 이 줄이 있어도 값이 그대로다.
        falloff = SampleShadow(surface.position, surface.normal, toLight);
    }
    else
    {
        const float3 delta = position.xyz - surface.position;
        const float  distance = length(delta);
        toLight = delta / max(distance, 1e-4f);

        // 반경 밖은 0. 컬링이 반경으로 자르므로 셰이딩도 같은 기준이라야
        // 경계에서 두 경로가 갈리지 않는다.
        const float radius = attenuation.w;
        if (distance > radius) return float3(0, 0, 0);

        falloff = PbrLocalLightFalloff(attenuation, distance);

        if (position.w > 1.5f)
        {
            // 스포트 — 원뿔 밖을 부드럽게 끈다(Deferred와 같은 식).
            // 이것이 없으면 투명면에서 스포트가 점광원처럼 사방을 비춘다.
            falloff *= PbrSpotFalloff(toLight, direction);
        }
    }

    if (falloff <= 0.0f) return float3(0.0f);
    return surface.EvaluateLight(toLight, color.rgb * color.a * falloff);
}

[shader("fragment")]
float4 PSMain(VSOut input, bool frontFace : SV_IsFrontFace) : SV_TARGET
{
    // Missing slots are semantic defaults; legacy fixtures may omit the table.
    const uint materialFlags = (uint)input.material.w;
    const bool hasTextures = (0u != (materialFlags & 1u));
    const bool useLegacyInstanceMaterial = (0u == (materialFlags & 2u));
    const float4 resolvedBaseColor = useLegacyInstanceMaterial
        ? input.baseColor : baseColor;
    const float resolvedRoughness = useLegacyInstanceMaterial
        ? input.material.y : roughness;
    const float resolvedMetallic = useLegacyInstanceMaterial
        ? input.material.x : metallic;
    const float resolvedNormalScale = useLegacyInstanceMaterial
        ? 1.0f : normalScale;
    // I5-M5 flow 승격 — snapshot draw의 wind/uvScroll 정본은 material CB(b2)다.
    // 인스턴스 채널은 legacy/self-test 폴백이고, 시간(z/w)은 항상 인스턴스에서
    // 온다.
    const float4 resolvedFlowWind = useLegacyInstanceMaterial
        ? input.flowWind : flowWindVector;
    const float2 resolvedFlowUv = useLegacyInstanceMaterial
        ? input.flowUvTime.xy : flowUvScroll;
    const float2 resolvedUv = input.uv
        + resolvedFlowUv * input.flowUvTime.z;
    const float2 resolvedUv1 = input.uv1 + resolvedFlowUv * input.flowUvTime.z;

    float4 baseSample = float4(1.0f);
    float3 mrSample = float3(1.0f);
    float3 emissionSample = useLegacyInstanceMaterial ? float3(0.0f) : float3(1.0f);
    if (hasTextures)
    {
        baseSample = FORWARD_BASE_COLOR_TEXTURE.Sample(gSampler, MaterialUv(16, resolvedUv, resolvedUv1));
        mrSample = ormMap.Sample(gSampler, MaterialUv(18, resolvedUv, resolvedUv1)).rgb;
        emissionSample = emissiveMap.Sample(gSampler, MaterialUv(19, resolvedUv, resolvedUv1)).rgb;
    }
    const PbrMaterial material = EvaluatePbrMaterial(baseSample, mrSample,
        emissionSample, resolvedBaseColor, resolvedRoughness, resolvedMetallic,
        useLegacyInstanceMaterial ? float3(1.0f) : emissive,
        useLegacyInstanceMaterial ? 1.0f : emissiveStrength);
    float4 albedo = material.albedo;
    albedo.a = ApplyPbrCoverage(albedo.a * (useLegacyInstanceMaterial ? 1.0 : input.vertexAlpha),
        useLegacyInstanceMaterial ? input.coverageCutoff : alphaCutoff, input.coverageFlags, frontFace);
    const float rough = saturate(material.roughness);
    const float materialMetallic = saturate(material.metallic);
    float3 representativeEmission = float3(0.0f);
    // P2d-b: authored speed/frequency와 immutable frame time·m_flowInfo를 함께
    // 사용한다. total=0, flow=0이면 P2c의 기존 authored phase와 정확히 같다.
    const float flowPhase = dot(resolvedFlowWind.xy, resolvedUv)
        + resolvedFlowWind.z * input.flowUvTime.z + resolvedFlowWind.w;
#if defined(FORWARD_WATER_MATERIAL)
    const float waterWave = 0.5f + 0.5f * sin(
        (resolvedUv.x + resolvedUv.y) * waveFrequency
        + waveSpeed * (1.0f + input.flowUvTime.z) + flowPhase);
    const float waterResponse = saturate(
        waterTint + waveAmplitude * waterWave);
    representativeEmission = material.emissive
        * float3(0.04f, 0.20f, 0.70f) * waterResponse;
#elif defined(FORWARD_WIND_MATERIAL)
    const float windWave = 0.5f + 0.5f * sin(
        (resolvedUv.x - resolvedUv.y) * windFrequency
        + windSpeed * (1.0f + input.flowUvTime.z) + flowPhase);
    const float windResponse = saturate(
        windTint + windStrength * windWave);
    representativeEmission = material.emissive
        * float3(0.08f, 0.65f, 0.14f) * windResponse;
#endif
    const bool useNormalMap = hasTextures && input.material.z != 0.0f;
    float3 normalSample = float3(0.5f, 0.5f, 1.0f);
    if (useNormalMap) normalSample = normalMap.Sample(gSampler, MaterialUv(17, resolvedUv, resolvedUv1)).rgb;
    float3 normal = EvaluatePbrNormal(input.normal, input.tangent,
        input.bitangent, normalSample, resolvedNormalScale, useNormalMap,
        SHADING_QUALITY == 1);
    normal = OrientPbrNormal(normal, input.coverageFlags, frontFace);
    const float3 view = normalize(gEyePosition.xyz - input.worldPos);
    float2 dfg = PbrNeutralDfg();
    if (gHasIbl != 0u)
        dfg = gBrdfLut.SampleLevel(gIblSampler,
            float2(PbrNdotV(normal, view), rough), 0.0f).rg;
    const PbrSurface surface = BuildPbrSurface(input.worldPos, normal, view,
        albedo.rgb, rough, materialMetallic, dfg);

    float3 lit = 0.0f;

#ifdef REFERENCE_PATH
    // 참조: 전 광원을 돈다(옛 포워드 방식).
    for (uint i = 0; i < gLightCount; ++i)
    {
        lit += Contribute(i, surface);
    }
#else
    // Forward+: 자기 타일의 목록만 돈다.
    const uint2 tile = uint2(input.position.xy) / TILE_SIZE;
    const uint  tileIndex = min(tile.y, gTileGrid.y - 1) * gTileGrid.x
        + min(tile.x, gTileGrid.x - 1);

    const uint count = gTileCount[tileIndex];
    for (uint i = 0; i < count; ++i)
    {
        const uint lightIndex = gTileList[tileIndex * MAX_LIGHTS_PER_TILE + i];
        lit += Contribute(lightIndex, surface);
    }
#endif

    // ── IBL 앰비언트 (split-sum, Deferred와 같은 식) ──
    //
    // 이것이 없으면 투명이 앰비언트 구간에서 불투명보다 어둡게 나온다.
    float3 ambient = 0.0f;
    if (0 != gHasIbl)
    {
        const float3 irradiance = gIrradiance.SampleLevel(gIblSampler, normal, 0.0f).rgb;

        const float3 reflection = reflect(-surface.view, normal);
        const float3 prefiltered = gPrefiltered.SampleLevel(gIblSampler,
            reflection, rough * (IBL_PREFILTER_MIPS - 1)).rgb;
        const float occlusion = EvaluatePbrOcclusion(
            hasTextures ? aoMap.Sample(gSampler, MaterialUv(20, resolvedUv, resolvedUv1)).r : 1.0,
            useLegacyInstanceMaterial ? 1.0 : occlusionStrength);
        const IblAmbient evaluated = EvaluateIblAmbient(albedo.rgb, surface.f0,
            rough, materialMetallic, surface.ndotv, occlusion,
            irradiance, prefiltered, dfg);
        ambient = evaluated.Sum();
    }

    // 알파는 베이스 컬러에서 온다 — 이것이 블렌드를 성립시킨다.
    return float4(lit + ambient + material.emissive + representativeEmission, albedo.a);
}
