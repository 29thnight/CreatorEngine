struct VSIn
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
};

struct VSOut
{
    float4 position  : SV_POSITION;
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
    // 온다. float2를 앞에 두는 이유: reflection이 CB 크기를 패딩 없이 보고하므로
    // float4가 마지막이어야 총합이 16B 정렬로 끝난다(pass 검증 조건).
    float2 flowUvScroll;
    float4 flowWindVector;
};

StructuredBuffer<ShadeInstance> gInstances : register(t0);
StructuredBuffer<float4>        gLights    : register(t1);
StructuredBuffer<uint>          gTileCount : register(t2);
StructuredBuffer<uint>          gTileList  : register(t3);

// M6-P2d-c: 물리 슬롯은 t4..t7/space0으로 유지하되 texture property 이름은
// ShaderMeta/HLSL reflection 정본이다. Wind fixture가 t4를 windMap으로 바꿔
// Standard 이름을 C++에서 하드코딩하지 않는 제품 경로를 판정한다.
#ifndef FORWARD_BASE_COLOR_TEXTURE
#define FORWARD_BASE_COLOR_TEXTURE baseColorMap
#endif
Texture2D FORWARD_BASE_COLOR_TEXTURE : register(t4);
Texture2D normalMap    : register(t5);
Texture2D ormMap       : register(t6);
Texture2D emissiveMap  : register(t7);

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
// 두 함수 모두 Deferred에서 그대로 가져왔다. GGX 삼총사와 같은 사정이라
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

VSOut VSMain(VSIn input, uint instanceId : SV_InstanceID)
{
    const ShadeInstance instance = gInstances[instanceId];

    const float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);

    VSOut output;
    output.position  = mul(worldPosition, gViewProjection);
    output.worldPos  = worldPosition.xyz;
    output.uv        = input.uv;
    output.baseColor = instance.baseColor;
    output.material  = float4(instance.metallic, instance.roughness,
        (float)instance.useNormalMap, (float)instance.materialFlags);
    // flow 승격의 CB 선택은 PS에서 한다 — VS가 b2를 읽으면 root signature의
    // CBV 스테이지 가시성이 어긋난다(실측: PSO 생성 E_INVALIDARG). 인스턴스
    // 채널은 그대로 나르고 PS가 materialFlags로 고른다.
    output.flowWind = instance.flowWindVector;
    output.flowUvTime = float4(instance.flowUvScroll,
        instance.flowTotalSeconds, instance.flowDeltaSeconds);

    // 법선·탄젠트는 회전만 적용한다(GBuffer와 같은 처리). 비균등 스케일에는
    // 역전치가 필요한데, 그건 GBuffer가 아직 안 하므로 여기서만 하면 두
    // 경로가 갈린다.
    output.normal    = mul(input.normal,    (float3x3)instance.world);
    output.tangent   = mul(input.tangent,   (float3x3)instance.world);
    output.bitangent = mul(input.bitangent, (float3x3)instance.world);
    return output;
}

// ── GGX 정반사 (EnhancedDeferredPass와 같은 식) ──
//
// 세 함수 모두 Deferred에서 그대로 가져왔다. 손으로 옮긴 것이라 값이
// 갈리면 같은 재질이 불투명과 투명에서 다르게 보인다 — 고칠 때는 두 곳을
// 함께 고칠 것.
float DistributionGGX(float ndoth, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * d * d, 1e-6f);
}

float VisibilitySmith(float ndotv, float ndotl, float alpha)
{
    // 높이 상관 Smith. 분모의 4*NdotV*NdotL까지 흡수한 형태라 호출부에서
    // 다시 나누지 않는다.
    const float a2 = alpha * alpha;
    const float v = ndotl * sqrt(ndotv * ndotv * (1.0f - a2) + a2);
    const float l = ndotv * sqrt(ndotl * ndotl * (1.0f - a2) + a2);
    return 0.5f / max(v + l, 1e-6f);
}

float3 FresnelSchlick(float3 f0, float vdoth)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - vdoth), 5.0f);
}

// 광원 하나의 기여. 컬링 경로와 참조 경로가 같은 코드를 쓴다 —
// 다른 것은 '어떤 광원을 도는가'뿐이어야 대조가 뜻을 갖는다.
// 셰이딩에 필요한 표면 값. 광원마다 같으므로 루프 밖에서 한 번 만든다.
struct Surface
{
    float3 worldPos;
    float3 normal;
    float3 viewDirection;
    float3 diffuseAlbedo;   // albedo / PI
    float3 f0;
    float  metallic;
    float  alpha;           // roughness^2
    float  ndotv;
};

float3 Contribute(uint lightIndex, Surface surface)
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
        falloff = SampleShadow(surface.worldPos, surface.normal, toLight);
    }
    else
    {
        const float3 delta = position.xyz - surface.worldPos;
        const float  distance = length(delta);
        toLight = delta / max(distance, 1e-6f);

        // 반경 밖은 0. 컬링이 반경으로 자르므로 셰이딩도 같은 기준이라야
        // 경계에서 두 경로가 갈리지 않는다.
        const float radius = attenuation.w;
        if (distance > radius) return float3(0, 0, 0);

        const float denominator = attenuation.x
            + attenuation.y * distance
            + attenuation.z * distance * distance;
        falloff = 1.0f / max(denominator, 1e-6f);

        // ★ 반경에서 0으로 잦아드는 테이퍼. Deferred가 쓰는 것과 같은 식이다
        //   (EnhancedDeferredPass의 광원 루프). 이것이 없으면 반경 경계에서
        //   딱 끊겨, 같은 광원 아래 불투명면은 부드럽게 어두워지는데 투명면만
        //   선이 생긴다 — 투명을 합성하기 전에는 안 보이던 어긋남이다.
        //   반경에서 정확히 0이라 위의 하드 컷과도 어긋나지 않는다.
        falloff *= saturate(1.0f - distance / max(radius, 1e-4f));

        if (position.w > 1.5f)
        {
            // 스포트 — 원뿔 밖을 부드럽게 끈다(Deferred와 같은 식).
            // 이것이 없으면 투명면에서 스포트가 점광원처럼 사방을 비춘다.
            const float cosAngle = dot(-toLight, normalize(direction.xyz));
            const float cutoff = cos(direction.w * 0.5f);
            falloff *= smoothstep(cutoff, lerp(cutoff, 1.0f, 0.2f), cosAngle);
        }
    }

    // ── BRDF (Deferred의 광원 루프와 같은 식) ──
    const float ndotl = saturate(dot(surface.normal, toLight));
    if (ndotl <= 0.0f || falloff <= 0.0f) return float3(0, 0, 0);

    const float3 halfVector = normalize(toLight + surface.viewDirection);
    const float  ndoth = saturate(dot(surface.normal, halfVector));
    const float  vdoth = saturate(dot(surface.viewDirection, halfVector));

    const float3 fresnel = FresnelSchlick(surface.f0, vdoth);
    const float3 specular = fresnel
        * DistributionGGX(ndoth, surface.alpha)
        * VisibilitySmith(surface.ndotv, ndotl, surface.alpha);

    // 에너지 보존: 반사되지 않은 몫만 확산으로 간다. 금속은 확산이 없다.
    const float3 kd = (1.0f - fresnel) * (1.0f - surface.metallic);

    const float3 radiance = color.rgb * color.a * ndotl * falloff;
    return (kd * surface.diffuseAlbedo + specular) * radiance;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    // ── 재질 ──
    //
    // 채널 규약은 GBuffer와 같다: ORM은 glTF대로 G가 거칠기, B가 금속성이고,
    // 거칠기는 계수를 곱하고 금속성은 더한다(GBuffer의 metalRough 조립과
    // 같은 식). occlusion(R)은 Deferred가 읽지 않으므로 여기서도 안 쓴다 —
    // 한쪽만 적용하면 같은 재질이 불투명과 투명에서 갈린다.
    //
    // ★ 텍스처는 묶였을 때만 읽는다. 없는 슬롯에 폴백을 물리는 방법도
    //   있지만(GBuffer가 그렇게 한다), 이 패스는 텍스처 캐시가 없는 자가
    //   검증에서도 돌아야 한다 — 그때는 만들 폴백조차 없다.
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

    float4 albedo = resolvedBaseColor;
    float3 materialEmissive = 0.0f;
    float3 representativeEmission = 0.0f;
    float  rough = resolvedRoughness;
    float  materialMetallic = resolvedMetallic;

    if (hasTextures)
    {
        albedo *= FORWARD_BASE_COLOR_TEXTURE.Sample(gSampler, resolvedUv);
        const float3 sampledEmissive = emissiveMap.Sample(gSampler, resolvedUv).rgb;
        materialEmissive = useLegacyInstanceMaterial
            ? sampledEmissive : sampledEmissive * emissive;

        const float3 orm = ormMap.Sample(gSampler, resolvedUv).rgb;
        rough = orm.g * resolvedRoughness;
        materialMetallic = orm.b + resolvedMetallic;
    }
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
    representativeEmission = emissive
        * float3(0.04f, 0.20f, 0.70f) * waterResponse;
#elif defined(FORWARD_WIND_MATERIAL)
    const float windWave = 0.5f + 0.5f * sin(
        (resolvedUv.x - resolvedUv.y) * windFrequency
        + windSpeed * (1.0f + input.flowUvTime.z) + flowPhase);
    const float windResponse = saturate(
        windTint + windStrength * windWave);
    representativeEmission = emissive
        * float3(0.08f, 0.65f, 0.14f) * windResponse;
#endif
    rough = saturate(rough);
    materialMetallic = saturate(materialMetallic);

    // ── 법선 ──
    float3 normal = normalize(input.normal);
    if (hasTextures && 0.0f != input.material.z)
    {
        // 접선 공간 변환. GBuffer와 같은 그람-슈미트 재직교화 + 저장된
        // 종법선에서 핸디드니스만 가져오는 처리다 — cross로 다시 만들면
        // UV가 뒤집힌 메시에서 조명이 반대로 나온다.
        const float3 n = normal;
        const float3 t = normalize(input.tangent - n * dot(n, input.tangent));
        const float  handedness = (dot(cross(n, t), input.bitangent) < 0.0f) ? -1.0f : 1.0f;
        const float3 b = cross(n, t) * handedness;

        float3 sampled = normalMap.Sample(gSampler, resolvedUv).rgb * 2.0f - 1.0f;
        sampled.xy *= resolvedNormalScale;
        normal = normalize(sampled.x * t + sampled.y * b + sampled.z * n);
    }
#if SHADING_QUALITY == 1
    // GBuffer의 reduced 축과 같은 의미: texture normal을 vertex normal 쪽으로
    // 완화한다. 0(full)은 P2b 이전 출력 정본이다.
    normal = normalize(normal + normalize(input.normal));
#endif

    // ── 표면 항 (Deferred와 같은 구성) ──
    Surface surface;
    surface.worldPos = input.worldPos;
    surface.normal = normal;
    surface.viewDirection = normalize(gEyePosition.xyz - input.worldPos);
    surface.ndotv = saturate(dot(normal, surface.viewDirection)) + 1e-5f;
    // 유전체의 기본 반사율 0.04는 관행값이고, 금속은 알베도가 곧 반사색이다.
    surface.f0 = lerp(0.04f, albedo.rgb, materialMetallic);
    surface.diffuseAlbedo = albedo.rgb / 3.14159265f;
    surface.metallic = materialMetallic;
    // 거칠기를 그대로 쓰면 0에서 정반사가 점 하나가 되어 사라진다. 제곱해
    // 쓰는 것이 관행이고 하한을 두어 완전 거울을 피한다.
    surface.alpha = max(rough * rough, 1e-3f);

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

        const float3 reflection = reflect(-surface.viewDirection, normal);
        const float3 prefiltered = gPrefiltered.SampleLevel(gIblSampler,
            reflection, rough * (IBL_PREFILTER_MIPS - 1)).rgb;
        const float2 environmentBrdf = gBrdfLut.SampleLevel(gIblSampler,
            float2(surface.ndotv, rough), 0.0f).rg;

        const float3 fresnelAmbient = FresnelSchlick(surface.f0, surface.ndotv);
        const float3 kdAmbient = (1.0f - fresnelAmbient) * (1.0f - materialMetallic);

        ambient = kdAmbient * irradiance * albedo.rgb
            + prefiltered * (surface.f0 * environmentBrdf.x + environmentBrdf.y);
    }

    // 알파는 베이스 컬러에서 온다 — 이것이 블렌드를 성립시킨다.
    return float4(lit + ambient + materialEmissive + representativeEmission, albedo.a);
}
