#include "Includes/Ibl.slang"

// Deferred 조명. GBuffer 를 읽어 직접광 + IBL 앰비언트를 적분한다.
//
// ★ Deferred.hlsl 의 이식본이다. 바인딩(b0, t0..t8, s0..s2)과 상수 레이아웃은
//   한 바이트도 바뀌지 않았다 ─ C++ 쪽 업로더가 그대로 쓴다. 바뀐 것은 BRDF
//   본체이고, 그 정본은 Includes/Brdf.slang 과 Includes/Ibl.slang 이다.
//
// ── HLSL 판과 달라진 것 ─────────────────────────────────────────────
//
//   · 조도 규약: irradiance 는 이제 E/PI 라 `* albedo` 만 한다. 굽는 쪽
//     (IblIrradiance.slang)과 반드시 함께 움직여야 한다.
//   · 앰비언트 프레넬이 거칠기 인식으로 바뀌었다(스치는 각 림 과잉 제거).
//   · 다중산란 보상이 붙었다 ─ 직접광은 Turquin, 앰비언트는 Fdez-Aguera.
//     거친 금속이 어두워지던 원인이고, 금이 금으로 보이게 하는 항이다.
//   · occlusion(R 채널)을 읽는 배선을 켰다. GBuffer 가 그 자리에 정의된 값을
//     써 넣게 된 뒤부터다 ─ 예전에는 소비자도 없었고 쓰던 값도 오클루전이
//     아니었다. PSMain 안의 근거 참조.

#define MAX_DEFERRED_LIGHTS 64
#define SHADOW_CASCADE_COUNT 3

// 생성기(EnhancedIBLGenerator::kPrefilterMips)와 같아야 한다.
#define IBL_PREFILTER_MIPS 6

struct DeferredLight
{
    float4 position;      // w = 타입 (0 방향광 · 1 점광 · 2 스포트)
    float4 direction;     // w = 스포트 각도
    float4 color;         // rgb 색 · a 세기
    float4 attenuation;   // x 상수 · y 선형 · z 이차 · w 반경
};

cbuffer LightingConstants : register(b0)
{
    float4x4      gInverseViewProjection;
    float4x4      gLightViewProjection[SHADOW_CASCADE_COUNT];
    float4        gEyePosition;
    float4        gCameraForward;
    float4        gCascadeSplits;   // xyz = 각 캐스케이드가 끝나는 뷰 깊이
    float4        gShadowBias;      // xyz = 캐스케이드별 깊이 편향 · w = 경사 계수
    uint          gLightCount;
    uint          gHasShadow;
    float         gCascadeBlendBand; // 경계 블렌딩 폭 · 0이면 하드 스위치
    uint          gHasIbl;
    DeferredLight gLights[MAX_DEFERRED_LIGHTS];
};

Texture2D      gDiffuse    : register(t0);
Texture2D      gMetalRough : register(t1);
Texture2D      gNormal     : register(t2);
Texture2D      gEmissive   : register(t3);
Texture2D      gDepth      : register(t4);
Texture2DArray gShadowMap  : register(t5);

TextureCube    gIrradiance  : register(t6);
TextureCube    gPrefiltered : register(t7);
Texture2D      gBrdfLut     : register(t8);

SamplerState gSampler    : register(s0);

// IBL 전용 선형 샘플러. GBuffer 읽기는 포인트(화면 픽셀 1:1)가 맞지만
// 큐브맵·LUT 는 보간 없이 읽으면 밴딩이 그대로 드러난다.
SamplerState gIblSampler : register(s2);

// 하드웨어가 이웃 텍셀 넷을 비교하고 평균내 준다(2x2 PCF).
SamplerComparisonState gShadowSampler : register(s1);

// ── 그림자 ──────────────────────────────────────────────────────────

float SampleShadowCascade(uint cascade, float3 worldPosition, float slopeTan)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0), gLightViewProjection[cascade]);
    const float3 projected  = lightSpace.xyz / lightSpace.w;

    // 라이트 상자 밖은 그림자 정보가 없다. 가려진 것으로 치면 맵 경계에
    // 검은 띠가 생기므로 빛을 받는 것으로 둔다.
    if (any(abs(projected.xy) > 1.0) || projected.z > 1.0) return 1.0;

    const float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);

    // 깊이 편향 = 캐스케이드 편향 x (1 + 경사 계수 x tan).
    const float bias = gShadowBias[cascade] * (1.0 + gShadowBias.w * slopeTan);

    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        float3(uv, float(cascade)), projected.z - bias);
}

float SampleShadow(float3 worldPosition, float3 normal, float3 toLight)
{
    if (gHasShadow == 0u) return 1.0;

    const float viewDepth = dot(worldPosition - gEyePosition.xyz, gCameraForward.xyz);

    uint cascade = 0u;
    if (viewDepth > gCascadeSplits.x) cascade = 1u;
    if (viewDepth > gCascadeSplits.y) cascade = 2u;

    // tan(경사각) = sqrt(1-cos^2)/cos. 수평에 가까울수록 폭주하므로 8로 자른다.
    const float ndotl    = saturate(dot(normal, toLight));
    const float slopeTan = min(sqrt(max(1.0 - ndotl * ndotl, 0.0)) / max(ndotl, 1e-3), 8.0);

    float shadow = SampleShadowCascade(cascade, worldPosition, slopeTan);

    // 경계 블렌딩 — 해상도가 다른 두 캐스케이드가 만나는 선을 지운다.
    if (gCascadeBlendBand > 0.0 && cascade < uint(SHADOW_CASCADE_COUNT) - 1u)
    {
        const float split     = (cascade == 0u) ? gCascadeSplits.x : gCascadeSplits.y;
        const float bandStart = split * (1.0 - gCascadeBlendBand);
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4));
            shadow = lerp(shadow,
                SampleShadowCascade(cascade + 1u, worldPosition, slopeTan), blend);
        }
    }

    return shadow;
}

// ── 표면 ────────────────────────────────────────────────────────────

// 광원마다 다시 유도하지 않는 항만 모은다. 직접광 루프가 L 에 의존하는
// 절반만 계산하도록 하는 것이 이 구조체의 존재 이유다.
struct SurfaceContext
{
    float3 position;
    float3 normal;
    float3 view;
    float  ndotv;
    float  alpha;          // roughness^2
    float3 f0;
    float3 diffuseBase;    // albedo * (1 - metallic) / PI
    float3 energyComp;     // 다중산란 보상(Turquin 2019)
    float  metallic;

    float3 EvaluateLight(float3 toLight, float3 radiance)
    {
        const float ndotl = saturate(dot(normal, toLight));
        if (ndotl <= 0.0) return float3(0.0);

        const float3 halfVector = normalize(toLight + view);
        const float  ndoth      = saturate(dot(normal, halfVector));
        const float  vdoth      = saturate(dot(view, halfVector));

        const float3 fresnel  = FresnelSchlick(f0, vdoth);
        const float3 specular = fresnel * energyComp
            * DistributionGGX(ndoth, alpha)
            * VisibilitySmithHeightCorrelated(ndotv, ndotl, alpha);

        // 에너지 보존: 반사되지 않은 몫만 확산으로 간다. diffuseBase 가
        // (1 - metallic) 을 이미 품고 있다.
        const float3 diffuse = (1.0 - fresnel) * diffuseBase;

        return (diffuse + specular) * radiance * ndotl;
    }
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없는 풀스크린 삼각형. 쿼드보다 낫다 — 대각선에서 픽셀이
    // 두 번 셰이딩되는 일이 없고, 정점도 셋뿐이다.
    VSOut output;
    output.uv = float2(float((id << 1u) & 2u), float(id & 2u));
    output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 PSMain(VSOut input) : SV_TARGET
{
    const float3 albedo   = gDiffuse.Sample(gSampler, input.uv).rgb;
    const float3 normal   = normalize(gNormal.Sample(gSampler, input.uv).rgb * 2.0 - 1.0);
    const float3 emissive = gEmissive.Sample(gSampler, input.uv).rgb;

    // GBuffer 규약은 R 오클루전 · G 거칠기 · B 금속성이다.
    const float3 orm      = gMetalRough.Sample(gSampler, input.uv).rgb;
    const float  rough     = saturate(orm.g);
    const float  metallic  = saturate(orm.b);
    const float  depth     = gDepth.Sample(gSampler, input.uv).r;

    // ★ occlusion(R)은 GBuffer 가 정의된 값을 써 넣게 된 뒤부터 읽는다.
    //
    //   임포터는 glTF의 metallicRoughness 텍스처를 ormMap 에 묶는데
    //   (SceneToModelDraft: metallicRoughnessMap → OrmMap), 규격상 그 텍스처의
    //   R 은 **미정의**다. 코퍼스를 재 보면 scene.glb 의 MR 텍스처 20 장은
    //   R 이 0~103 이고 그중 하나는 전 텍셀이 0 이다 — 그 원본을 그대로
    //   소비했다면 그 씬은 앰비언트가 통째로 죽었을 것이다.
    //
    //   이제 GBuffer.slang 이 그 자리에 중립 1 을 써 넣는다(aoMap 슬롯이
    //   생기기 전까지). 그래서 여기 R 은 "AO 아니면 1" 이 보장되고, 슬롯이
    //   생기는 날 이 소비 코드는 손댈 곳이 없다.
    const float occlusion = saturate(orm.r);

    // 깊이에서 월드 위치를 복원한다. GBuffer 에 위치를 따로 저장하면 타깃이
    // 하나 늘고 대역폭이 그만큼 든다 — 깊이가 이미 있으니 역행렬로 푼다.
    const float2 ndc              = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    const float4 worldHomogeneous = mul(float4(ndc, depth, 1.0), gInverseViewProjection);
    const float3 worldPosition    = worldHomogeneous.xyz / worldHomogeneous.w;

    SurfaceContext surface;
    surface.position = worldPosition;
    surface.normal   = normal;
    surface.view     = normalize(gEyePosition.xyz - worldPosition);
    surface.ndotv    = saturate(dot(normal, surface.view)) + 1e-5;

    // 거칠기를 그대로 쓰면 0 에서 정반사가 점 하나가 되어 화면에서 사라진다.
    // 제곱해 쓰는 것이 관행이고, 하한을 두어 완전 거울을 피한다.
    surface.alpha = max(rough * rough, 1e-3);

    // 유전체의 기본 반사율 0.04 는 관행값이다(대부분의 비금속이 4% 근처).
    // 금속은 알베도가 곧 반사색이다.
    surface.f0       = lerp(float3(0.04), albedo, metallic);
    surface.metallic = metallic;

    // BRDF LUT 는 픽셀당 한 번만 읽는다. 에너지 보상과 앰비언트가 같은
    // (NdotV, roughness) 로 같은 값을 원하므로 나눠 읽을 이유가 없다.
    const float2 dfg = gBrdfLut.SampleLevel(gIblSampler,
        float2(surface.ndotv, rough), 0.0).rg;

    surface.diffuseBase = albedo * (1.0 - metallic) * INV_PI;
    surface.energyComp  = SpecularEnergyCompensation(surface.f0, dfg);

    float3 lit = float3(0.0);
    for (uint i = 0u; i < gLightCount; ++i)
    {
        const DeferredLight light = gLights[i];
        const uint type = uint(light.position.w);

        float3 toLight;
        float  attenuation = 1.0;

        if (type == 0u)
        {
            // 방향광 — 위치와 감쇠가 없다. 그림자는 이쪽에만 붙는다.
            toLight     = -normalize(light.direction.xyz);
            attenuation = SampleShadow(worldPosition, normal, toLight);
        }
        else
        {
            const float3 offset   = light.position.xyz - worldPosition;
            const float  distance = length(offset);
            toLight = offset / max(distance, 1e-4);

            attenuation = 1.0 / (light.attenuation.x
                + light.attenuation.y * distance
                + light.attenuation.z * distance * distance);

            // 반경 밖은 잘라낸다. 감쇠식만 쓰면 먼 광원이 미세하게 계속
            // 기여해 광원 수에 비례해 화면이 밝아진다.
            attenuation *= saturate(1.0 - distance / max(light.attenuation.w, 1e-4));

            if (type == 2u)
            {
                // 스포트 — 원뿔 밖을 부드럽게 끈다.
                const float cosAngle = dot(-toLight, normalize(light.direction.xyz));
                const float cutoff   = cos(light.direction.w * 0.5);
                attenuation *= smoothstep(cutoff, lerp(cutoff, 1.0, 0.2), cosAngle);
            }
        }

        if (attenuation <= 0.0) continue;

        const float3 radiance = light.color.rgb * light.color.a * attenuation;
        lit += surface.EvaluateLight(toLight, radiance);
    }

    // ── IBL 앰비언트 (split-sum + 다중산란) ──
    //
    // IBL 이 없으면 0 — 기존 동작(앰비언트 없음) 그대로다.
    float3 ambient = float3(0.0);
    if (gHasIbl != 0u)
    {
        const float3 irradiance = gIrradiance.SampleLevel(gIblSampler, normal, 0.0).rgb;

        const float3 reflection  = reflect(-surface.view, normal);
        const float3 prefiltered = gPrefiltered.SampleLevel(gIblSampler,
            reflection, rough * float(IBL_PREFILTER_MIPS - 1)).rgb;

        const IblAmbient evaluated = EvaluateIblAmbient(albedo, surface.f0, rough,
            metallic, surface.ndotv, occlusion, irradiance, prefiltered, dfg);
        ambient = evaluated.Sum();
    }

    return float4(lit + ambient + emissive, 1.0);
}
