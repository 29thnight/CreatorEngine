#define MAX_DEFERRED_LIGHTS 64

struct DeferredLight
{
    float4 position;      // w = 타입 (0 방향광 · 1 점광 · 2 스포트)
    float4 direction;     // w = 스포트 각도
    float4 color;         // rgb 색 · a 세기
    float4 attenuation;   // x 상수 · y 선형 · z 이차 · w 반경
};

#define SHADOW_CASCADE_COUNT 3

cbuffer LightingConstants : register(b0)
{
    float4x4      gInverseViewProjection;
    float4x4      gLightViewProjection[SHADOW_CASCADE_COUNT];
    float4        gEyePosition;
    float4        gCameraForward;
    float4        gCascadeSplits;   // xyz = 각 캐스케이드가 끝나는 뷰 깊이
    float4        gShadowBias;      // xyz = 캐스케이드별 깊이 편향 · w = 경사 비례 계수
    uint          gLightCount;
    uint          gHasShadow;
    float         gCascadeBlendBand; // 경계 블렌딩 폭(분할 깊이 비율) · 0이면 하드 스위치
    uint          gHasIbl;
    DeferredLight gLights[MAX_DEFERRED_LIGHTS];
};

Texture2D    gDiffuse    : register(t0);
Texture2D    gMetalRough : register(t1);
Texture2D    gNormal     : register(t2);
Texture2D    gEmissive   : register(t3);
Texture2D    gDepth      : register(t4);
Texture2DArray gShadowMap : register(t5);

// IBL 산출물(3-6 IBL 소비). 조도는 법선으로, 프리필터는 반사 방향 + 거칠기
// 밉으로, LUT는 (NdotV, 거칠기)로 읽는다 — split-sum 관행 그대로다.
TextureCube  gIrradiance  : register(t6);
TextureCube  gPrefiltered : register(t7);
Texture2D    gBrdfLut     : register(t8);

// 생성기(EnhancedIBLGenerator::kPrefilterMips)와 같아야 한다.
#define IBL_PREFILTER_MIPS 6

SamplerState gSampler    : register(s0);

// IBL 전용 선형 샘플러. GBuffer 읽기는 포인트(화면 픽셀 1:1)가 맞지만
// 큐브맵·LUT는 보간 없이 읽으면 밴딩이 그대로 드러난다.
SamplerState gIblSampler : register(s2);

// 비교 샘플러. 하드웨어가 이웃 텍셀 넷을 비교하고 평균내 준다(2x2 PCF) —
// 직접 네 번 읽는 것보다 싸고, 가장자리가 부드러워진다.
SamplerComparisonState gShadowSampler : register(s1);

// 한 캐스케이드에서의 그림자 표본. slopeTan은 호출부가 한 번만 계산한다.
float SampleShadowCascade(uint cascade, float3 worldPosition, float slopeTan)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0f), gLightViewProjection[cascade]);
    const float3 projected = lightSpace.xyz / lightSpace.w;

    // 라이트 상자 밖은 그림자 정보가 없다. 가려진 것으로 치면 맵 경계에
    // 검은 띠가 생기므로 빛을 받는 것으로 둔다. 마지막 캐스케이드 너머
    // (그림자 거리 밖)도 이 경로로 걸러진다.
    if (any(abs(projected.xy) > 1.0f) || projected.z > 1.0f) return 1.0f;

    const float2 uv = float2(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f);

    // 깊이 편향 = 캐스케이드 편향 x (1 + 경사 계수 x tan). 상수 항은 먼
    // 캐스케이드일수록 크고(텍셀이 넓다), 경사 항은 표면이 빛과 비스듬할수록
    // 크다 — 텍셀 하나 안에서 실제 깊이가 tan만큼 변하기 때문이다.
    const float bias = gShadowBias[cascade] * (1.0f + gShadowBias.w * slopeTan);

    return gShadowMap.SampleCmpLevelZero(gShadowSampler,
        float3(uv, (float)cascade), projected.z - bias);
}

// 방향광 캐스케이드 그림자. 반환값 1은 빛을 받음, 0은 가려짐.
// normal·toLight는 경사 비례 편향의 재료다 — 호출부(광원 루프)가 이미 갖고 있다.
float SampleShadow(float3 worldPosition, float3 normal, float3 toLight)
{
    if (gHasShadow == 0) return 1.0f;

    // 어느 캐스케이드인지는 카메라로부터의 뷰 깊이로 고른다. 캐스케이드를
    // 하나씩 투영해 보고 들어맞는 것을 찾는 방법도 있지만 그쪽은 행렬 곱이
    // 최대 셋이다 — 분할 지점이 뷰 깊이 기준이라 비교 둘이면 같은 답이 나온다.
    const float viewDepth = dot(worldPosition - gEyePosition.xyz, gCameraForward.xyz);

    uint cascade = 0;
    if (viewDepth > gCascadeSplits.x) cascade = 1;
    if (viewDepth > gCascadeSplits.y) cascade = 2;

    // tan(경사각) = sqrt(1-cos²)/cos. 수평에 가까울수록 폭주하므로 8로 자른다 —
    // 그 너머는 어차피 ndotl이 0에 가까워 빛 기여 자체가 사라진다.
    const float ndotl = saturate(dot(normal, toLight));
    const float slopeTan = min(sqrt(max(1.0f - ndotl * ndotl, 0.0f)) / max(ndotl, 1e-3f), 8.0f);

    float shadow = SampleShadowCascade(cascade, worldPosition, slopeTan);

    // 경계 블렌딩 — 분할 앞 구간에서 다음 캐스케이드와 섞는다. 두 캐스케이드는
    // 해상도가 달라 그림자 가장자리가 미세하게 어긋나는데, 하드 스위치면 그
    // 어긋남이 경계에서 선으로 보인다. 비용은 구간 안에서만 표본 하나 추가다.
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

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없는 풀스크린 삼각형. 쿼드보다 낫다 — 대각선에서 픽셀이
    // 두 번 셰이딩되는 일이 없고, 정점도 셋뿐이다.
    VSOut output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

// ── GGX 정반사 ──
//
// 거칠기·금속성을 확산 감쇠로만 쓰던 것을 실제 BRDF로 바꾼다. 그 전에는
// 재질 격자를 그려도 가로(거칠기)·세로(금속성)가 거의 같아 보였다 —
// 값이 셰이더에 닿는지는 확인할 수 있어도 '무엇을 하는지'는 볼 수 없었다.
//
// Trowbridge-Reitz(GGX) 분포 + Smith 높이 상관 가시성 + Schlick 프레넬.
// glTF/UE4가 쓰는 조합이라 저작 도구에서 만든 값이 의도대로 보인다.
float DistributionGGX(float ndoth, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = ndoth * ndoth * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * d * d, 1e-6f);
}

float VisibilitySmith(float ndotv, float ndotl, float alpha)
{
    // 높이 상관(height-correlated) Smith. 분모의 4*NdotV*NdotL까지 흡수한 형태라
    // 호출부에서 다시 나누지 않는다.
    const float a2 = alpha * alpha;
    const float v = ndotl * sqrt(ndotv * ndotv * (1.0f - a2) + a2);
    const float l = ndotv * sqrt(ndotl * ndotl * (1.0f - a2) + a2);
    return 0.5f / max(v + l, 1e-6f);
}

float3 FresnelSchlick(float3 f0, float vdoth)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - vdoth), 5.0f);
}

float4 PSMain(VSOut input) : SV_TARGET
{
    const float3 albedo   = gDiffuse.Sample(gSampler, input.uv).rgb;
    const float3 normal   = normalize(gNormal.Sample(gSampler, input.uv).rgb * 2.0f - 1.0f);
    const float3 emissive = gEmissive.Sample(gSampler, input.uv).rgb;
    const float2 orm      = gMetalRough.Sample(gSampler, input.uv).gb;
    const float  rough    = saturate(orm.x);
    const float  metallic = saturate(orm.y);
    const float  depth    = gDepth.Sample(gSampler, input.uv).r;

    // 깊이에서 월드 위치를 복원한다. GBuffer에 위치를 따로 저장하는 방법도 있지만
    // 타깃 하나가 더 늘고 대역폭이 그만큼 든다 — 깊이가 이미 있으니 역행렬로 푼다.
    const float2 ndc = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    const float4 clipPosition = float4(ndc, depth, 1.0f);
    const float4 worldHomogeneous = mul(clipPosition, gInverseViewProjection);
    const float3 worldPosition = worldHomogeneous.xyz / worldHomogeneous.w;

    // 시선 방향과 재질 항은 광원마다 같으므로 루프 밖에서 한 번만 만든다.
    const float3 viewDirection = normalize(gEyePosition.xyz - worldPosition);
    const float  ndotv = saturate(dot(normal, viewDirection)) + 1e-5f;

    // 유전체의 기본 반사율 0.04는 관행값이다(대부분의 비금속이 4% 근처).
    // 금속은 알베도가 곧 반사색이다.
    const float3 f0 = lerp(0.04f, albedo, metallic);
    const float3 diffuseAlbedo = albedo / 3.14159265f;

    // 거칠기를 그대로 쓰면 0에서 정반사가 점 하나가 되어 화면에서 사라진다.
    // 제곱해 쓰는 것이 관행이고, 하한을 두어 완전 거울을 피한다.
    const float alpha = max(rough * rough, 1e-3f);

    float3 lit = 0.0f;
    for (uint i = 0; i < gLightCount; ++i)
    {
        const DeferredLight light = gLights[i];
        const uint type = (uint)light.position.w;

        float3 toLight;
        float  attenuation = 1.0f;

        if (type == 0)
        {
            // 방향광 — 위치와 감쇠가 없다. 그림자는 이쪽에만 붙는다.
            toLight = -normalize(light.direction.xyz);
            attenuation = SampleShadow(worldPosition, normal, toLight);
        }
        else
        {
            const float3 offset = light.position.xyz - worldPosition;
            const float  distance = length(offset);
            toLight = offset / max(distance, 1e-4f);

            attenuation = 1.0f / (light.attenuation.x
                + light.attenuation.y * distance
                + light.attenuation.z * distance * distance);

            // 반경 밖은 잘라낸다. 감쇠식만 쓰면 먼 광원이 미세하게 계속 기여해
            // 광원 수에 비례해 화면이 밝아진다.
            attenuation *= saturate(1.0f - distance / max(light.attenuation.w, 1e-4f));

            if (type == 2)
            {
                // 스포트 — 원뿔 밖을 부드럽게 끈다.
                const float cosAngle = dot(-toLight, normalize(light.direction.xyz));
                const float cutoff = cos(light.direction.w * 0.5f);
                attenuation *= smoothstep(cutoff, lerp(cutoff, 1.0f, 0.2f), cosAngle);
            }
        }

        const float ndotl = saturate(dot(normal, toLight));
        if (ndotl <= 0.0f || attenuation <= 0.0f) continue;

        const float3 halfVector = normalize(toLight + viewDirection);
        const float  ndoth = saturate(dot(normal, halfVector));
        const float  vdoth = saturate(dot(viewDirection, halfVector));

        const float3 fresnel = FresnelSchlick(f0, vdoth);
        const float3 specular = fresnel
            * DistributionGGX(ndoth, alpha)
            * VisibilitySmith(ndotv, ndotl, alpha);

        // 에너지 보존: 반사되지 않은 몫만 확산으로 간다. 금속은 확산이 없다.
        const float3 kd = (1.0f - fresnel) * (1.0f - metallic);

        const float3 radiance = light.color.rgb * light.color.a * ndotl * attenuation;
        lit += (kd * diffuseAlbedo + specular) * radiance;
    }

    // ── IBL 앰비언트 (split-sum) ──
    //
    // 확산은 법선 방향 조도, 정반사는 반사 방향 프리필터 x LUT의 (scale, bias).
    // IBL이 없으면 0 — 기존 동작(앰비언트 없음) 그대로다.
    float3 ambient = 0.0f;
    if (gHasIbl != 0)
    {
        const float3 irradiance = gIrradiance.SampleLevel(gIblSampler, normal, 0.0f).rgb;

        const float3 reflection = reflect(-viewDirection, normal);
        const float3 prefiltered = gPrefiltered.SampleLevel(gIblSampler,
            reflection, rough * (IBL_PREFILTER_MIPS - 1)).rgb;
        const float2 environmentBrdf = gBrdfLut.SampleLevel(gIblSampler,
            float2(ndotv, rough), 0.0f).rg;

        const float3 fresnelAmbient = FresnelSchlick(f0, ndotv);
        const float3 kdAmbient = (1.0f - fresnelAmbient) * (1.0f - metallic);

        ambient = kdAmbient * irradiance * albedo
            + prefiltered * (f0 * environmentBrdf.x + environmentBrdf.y);
    }

    return float4(lit + ambient + emissive, 1.0f);
}
