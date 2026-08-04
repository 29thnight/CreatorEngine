#pragma once
#ifndef DYNAMICCPP_EXPORTS

// 포스트 체인 셰이더 (PHASE 3-6).
//
// 헤더로 뺀 이유는 SSGI와 같다 — 셰이더가 길어지면 C++ 로직이 그 사이에
// 파묻혀서, 무엇이 배선이고 무엇이 계산인지 읽어 내기 어려워진다.
namespace PostChainShaders
{
    // 모든 패스가 같은 상수 버퍼를 쓴다. 필드를 나눠 두면 패스마다 구조체가
    // 생기고, 그러면 '어느 패스가 어느 크기를 봤는가'가 갈릴 자리가 는다 —
    // SSGI에서 실제로 필터가 다른 해상도를 본 적이 있다.
    constexpr const char* kCommon = R"(
cbuffer PostParams : register(b0)
{
    uint2  gSrcSize;
    uint2  gDstSize;

    float  gBloomThreshold;
    float  gBloomKnee;
    float  gBloomIntensity;
    float  gExposure;

    float  gVignetteRadius;
    float  gVignetteSoftness;
    float  gSaturation;
    float  gContrast;

    float  gFxaaBias;
    float  gFxaaBiasMin;
    float  gFxaaSpanMax;
    uint   gFlags;        // 1 블룸 · 2 톤맵 · 4 비네트 · 8 그레이딩
};

// 선형 샘플러. FXAA가 소수 좌표를 읽어야 해서 필요하다 —
// 자세한 사연은 아래 FXAA 주석에 있다.
SamplerState gLinear : register(s0);

static const uint kFlagBloom    = 1u;
static const uint kFlagToneMap  = 2u;
static const uint kFlagVignette = 4u;
static const uint kFlagGrading  = 8u;
static const uint kFlagAgX      = 16u;   // 없으면 ACES

// 화면 밝기. 톤맵·블룸 임계·FXAA가 모두 같은 정의를 써야 한다 —
// 다르면 '임계는 넘겼는데 톤맵은 안 넘긴' 구간이 생겨 경계가 튄다.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// 정수 좌표를 원본 좌표로 옮긴다. 크기가 다른 두 텍스처를 오갈 때
// 0.5 픽셀 중심을 빼먹으면 반 픽셀씩 밀리는데, 그 증상은 '조금 흐리다'로만
// 보여 원인을 짚기 어렵다.
float2 DstToSrcUV(uint2 dst)
{
    return (float2(dst) + 0.5f) / float2(gDstSize);
}

float4 LoadSrc(Texture2D<float4> tex, float2 uv)
{
    const int2 coord = clamp(int2(uv * float2(gSrcSize)),
        int2(0, 0), int2(gSrcSize) - 1);
    return tex.Load(int3(coord, 0));
}
)";

    // ── 블룸 임계 ──
    //
    // 임계를 넘는 부분만 남긴다. 딱 잘라내면 경계에서 계단이 보이므로,
    // knee 구간에서 이차식으로 부드럽게 올린다(업계 통용 방식).
    constexpr const char* kThreshold = R"(
Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    // 2x2를 모아 내려간다. 한 점만 집으면 밝은 점 하나가 깜빡일 때
    // 블룸 전체가 같이 깜빡인다.
    const float2 uv = DstToSrcUV(id.xy);
    const float2 texel = 1.0f / float2(gSrcSize);

    float3 color = 0.0f;
    [unroll]
    for (int dy = 0; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = 0; dx <= 1; ++dx)
        {
            color += LoadSrc(gSource,
                uv + float2(dx - 0.5f, dy - 0.5f) * texel).rgb;
        }
    }
    color *= 0.25f;

    const float brightness = max(color.r, max(color.g, color.b));

    // soft knee. threshold-knee 아래는 0, threshold 위는 그대로,
    // 사이는 이차식으로 잇는다.
    const float knee = max(gBloomKnee, 1e-4f);
    float soft = brightness - gBloomThreshold + knee;
    soft = clamp(soft, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee);

    const float contribution =
        max(soft, brightness - gBloomThreshold) / max(brightness, 1e-4f);

    gOutput[id.xy] = float4(color * contribution, 1.0f);
}
)";

    // ── 다운샘플 ──
    //
    // 13탭 가중 평균(Call of Duty 발표에서 널리 퍼진 배치). 단순 2x2로
    // 내려가면 단수를 거듭할수록 격자 무늬가 생긴다 — 표본이 축에만
    // 몰려 있기 때문이다.
    constexpr const char* kDownsample = R"(
Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    const float2 uv = DstToSrcUV(id.xy);
    const float2 t = 1.0f / float2(gSrcSize);

    const float3 a = LoadSrc(gSource, uv + float2(-2, -2) * t).rgb;
    const float3 b = LoadSrc(gSource, uv + float2( 0, -2) * t).rgb;
    const float3 c = LoadSrc(gSource, uv + float2( 2, -2) * t).rgb;
    const float3 d = LoadSrc(gSource, uv + float2(-1, -1) * t).rgb;
    const float3 e = LoadSrc(gSource, uv + float2( 1, -1) * t).rgb;
    const float3 f = LoadSrc(gSource, uv + float2(-2,  0) * t).rgb;
    const float3 g = LoadSrc(gSource, uv                    ).rgb;
    const float3 h = LoadSrc(gSource, uv + float2( 2,  0) * t).rgb;
    const float3 i = LoadSrc(gSource, uv + float2(-1,  1) * t).rgb;
    const float3 j = LoadSrc(gSource, uv + float2( 1,  1) * t).rgb;
    const float3 k = LoadSrc(gSource, uv + float2(-2,  2) * t).rgb;
    const float3 l = LoadSrc(gSource, uv + float2( 0,  2) * t).rgb;
    const float3 m = LoadSrc(gSource, uv + float2( 2,  2) * t).rgb;

    float3 result = (d + e + i + j) * 0.125f;
    result += (a + b + g + f) * 0.03125f;
    result += (b + c + h + g) * 0.03125f;
    result += (f + g + l + k) * 0.03125f;
    result += (g + h + m + l) * 0.03125f;

    gOutput[id.xy] = float4(result, 1.0f);
}
)";

    // ── 업샘플 + 누적 ──
    //
    // 거친 밉을 텐트 필터로 올려 더한다. 출력에 더하는 것이라 목적지가
    // 이미 담고 있는 값을 읽는다 — UAV 읽기·쓰기가 같은 픽셀이라 안전하다.
    constexpr const char* kUpsample = R"(
Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    const float2 uv = DstToSrcUV(id.xy);
    const float2 t = 1.0f / float2(gSrcSize);

    // 3x3 텐트. 가중치 합이 1이라 밝기가 보존된다.
    float3 result = LoadSrc(gSource, uv + float2(-1, -1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2( 0, -1) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2( 1, -1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2(-1,  0) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv                    ).rgb * 4.0f;
    result += LoadSrc(gSource, uv + float2( 1,  0) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2(-1,  1) * t).rgb * 1.0f;
    result += LoadSrc(gSource, uv + float2( 0,  1) * t).rgb * 2.0f;
    result += LoadSrc(gSource, uv + float2( 1,  1) * t).rgb * 1.0f;
    result *= (1.0f / 16.0f);

    gOutput[id.xy] += float4(result, 0.0f);
}
)";

    // ── Uber: 블룸 합성 + 톤맵 + 비네트 + 그레이딩 ──
    //
    // 넷 다 그 픽셀 하나만 보고 답이 나오는 연산이라 한 번에 끝낸다.
    // 기존은 이것을 네 패스로 나눠 화면을 네 번 왕복했다.
    //
    // 순서는 기존 체인과 같게 둔다: 블룸 → 톤맵 → 비네트 → 그레이딩.
    // 순서가 바뀌면 그림이 달라지는데(톤맵 뒤의 비네트와 앞의 비네트는
    // 다른 결과다), 그 차이가 이식 실수인지 의도인지 구분되지 않는다.
    constexpr const char* kUber = R"(
Texture2D<float4> gColor : register(t0);
Texture2D<float4> gBloom : register(t1);

RWTexture2D<float4> gOutput : register(u0);

// ACES 근사(Narkowicz). 채널마다 따로 곡선을 먹인다 — 그래서 채도가 높은
// 밝은 색은 한 채널만 먼저 포화하고 나머지가 못 따라가 색이 밀린다.
float3 ToneMapACES(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ── AgX ──
//
// 세 단계다: ① 채널을 섞는 행렬 ② 로그 공간에서 시그모이드 ③ 역행렬.
//
// ①이 AgX의 핵심이다. 곡선을 먹이기 전에 채널을 서로 섞어 두면, 한 채널이
// 포화할 때 나머지도 같이 올라가므로 밝아질수록 흰색으로 수렴한다. ACES가
// 채널을 따로 다뤄 색을 밀어 버리는 자리에서 AgX는 색상을 유지한다.
//
// ②를 로그 공간에서 하는 이유는 노출이 곱셈이기 때문이다. 로그에서는 그것이
// 덧셈이 되어, 노출을 바꿔도 곡선의 모양(대비)이 변하지 않는다.
//
// 행렬과 다항식 계수는 Blender 4.0이 쓰는 AgX에서 왔다. 손으로 유도할 수
// 있는 값이 아니므로 그대로 둔다 — 여기 적힌 수를 바꾸면 AgX가 아니게 된다.
static const float3x3 kAgxMat = float3x3(
    0.842479062253094f,  0.0423282422610123f, 0.0423756549057051f,
    0.0784335999999992f, 0.878468636469772f,  0.0784336f,
    0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f);

static const float3x3 kAgxMatInv = float3x3(
     1.19687900512017f,  -0.0528968517574562f, -0.0529716355144438f,
    -0.0980208811401368f, 1.15190312990417f,   -0.0980434501171241f,
    -0.0990297440797205f,-0.0989611768448433f,  1.15107367264116f);

// 6차 다항식. 실제 AgX가 쓰는 시그모이드를 근사한 것이다.
float3 AgxContrast(float3 x)
{
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5f * x4 * x2
        - 40.14f * x4 * x
        + 31.96f * x4
        - 6.868f * x2 * x
        + 0.4298f * x2
        + 0.1191f * x
        - 0.00232f;
}

float3 ToneMapAgX(float3 x)
{
    // AgX가 담는 노출 범위. 아래는 -12.47EV, 위는 +4.03EV다.
    const float minEv = -12.47393f;
    const float maxEv = 4.026069f;

    // ★ 0과 음수를 먼저 막는다. log2(0)은 -inf, log2(음수)는 NaN이고,
    //   NaN은 clamp를 그대로 통과해 화면에 검은 점으로 남는다 —
    //   그 증상은 '가끔 픽셀이 튄다'로만 보여 원인을 짚기 어렵다.
    float3 v = mul(kAgxMat, max(x, 1e-10f));

    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = AgxContrast(v);

    return saturate(mul(kAgxMatInv, v));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    float3 color = gColor.Load(int3(id.xy, 0)).rgb;

    // ① 블룸 합성
    if (0 != (gFlags & kFlagBloom))
    {
        const float2 uv = (float2(id.xy) + 0.5f) / float2(gDstSize);
        const int2 bloomCoord = clamp(int2(uv * float2(gSrcSize)),
            int2(0, 0), int2(gSrcSize) - 1);
        color += gBloom.Load(int3(bloomCoord, 0)).rgb * gBloomIntensity;
    }

    // ② 톤맵
    //
    // 톤맵을 안 하는 경우에 saturate를 걸지 않는다. 참조 경로(단계를 나눠
    // 도는 옛 방식)에서는 블룸 합성 결과가 아직 HDR인 채로 다음 단계에
    // 넘어가야 하는데, 여기서 잘라 버리면 그 뒤의 톤맵이 볼 것이 없다.
    // 최종 목적지가 UNORM이면 그쪽이 알아서 클램프한다.
    if (0 != (gFlags & kFlagToneMap))
    {
        color = (0 != (gFlags & kFlagAgX))
            ? ToneMapAgX(color * gExposure)
            : ToneMapACES(color * gExposure);
    }

    // ③ 비네트 — 화면 중심에서 멀수록 어둡게.
    if (0 != (gFlags & kFlagVignette))
    {
        const float2 uv = (float2(id.xy) + 0.5f) / float2(gDstSize);
        const float dist = length(uv - 0.5f) * 2.0f;
        const float v = smoothstep(gVignetteRadius + gVignetteSoftness,
            gVignetteRadius - gVignetteSoftness, dist);
        color *= v;
    }

    // ④ 그레이딩 — 채도·대비.
    if (0 != (gFlags & kFlagGrading))
    {
        const float luma = Luminance(color);
        color = lerp(luma.xxx, color, gSaturation);
        color = saturate((color - 0.5f) * gContrast + 0.5f);
    }

    gOutput[id.xy] = float4(color, 1.0f);
}
)";

    // ── FXAA ──
    //
    // 톤맵 뒤(LDR)에서 돈다. HDR에서 돌리면 밝은 곳의 휘도 대비가 실제
    // 화면보다 훨씬 크게 나와서, 보이지도 않는 경계를 흐리고 정작 보이는
    // 경계는 놓친다 — 기존 체인이 그 순서였다.
    constexpr const char* kFxaa = R"(
Texture2D<float4> gSource : register(t0);
RWTexture2D<float4> gOutput : register(u0);

// ★ 소수 좌표를 선형 샘플러로 읽는다. 정수 Load로는 FXAA가 성립하지 않는다.
//
//   처음에 Load(int3(p + int2(dir * k), 0))로 썼다가 검증에서 '이웃 차이
//   0.0% 감소'가 나왔다. 추적해 보니 rgbA를 만드는 표본 둘의 오프셋이
//   각각 +0.868과 -0.868이었고, int2()가 그것을 0으로 잘라서 둘 다 자기
//   자신을 읽고 있었다. 평균을 내도 원본 그대로다.
//
//   FXAA의 표본 위치는 애초에 픽셀 사이(1/3, 2/3 지점)를 노린 것이라
//   소수 좌표가 본질이다. 정수로 반올림하면 남는 것이 없다.
float3 SampleAt(float2 uv)
{
    return gSource.SampleLevel(gLinear, uv, 0).rgb;
}

float LumaAt(int2 coord)
{
    const int2 c = clamp(coord, int2(0, 0), int2(gSrcSize) - 1);
    return Luminance(gSource.Load(int3(c, 0)).rgb);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gDstSize)) return;

    const int2 p = int2(id.xy);
    const float2 texel = 1.0f / float2(gSrcSize);
    const float2 uv = (float2(p) + 0.5f) * texel;

    const float lumaM  = LumaAt(p);
    const float lumaNW = LumaAt(p + int2(-1, -1));
    const float lumaNE = LumaAt(p + int2( 1, -1));
    const float lumaSW = LumaAt(p + int2(-1,  1));
    const float lumaSE = LumaAt(p + int2( 1,  1));

    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // 대비가 작으면 경계가 아니다. 그냥 통과시킨다 — 여기서 안 걸러내면
    // 평평한 면까지 흐려져 화면 전체가 뿌옇게 된다.
    if ((lumaMax - lumaMin) < max(gFxaaBiasMin, lumaMax * gFxaaBias))
    {
        gOutput[id.xy] = gSource.Load(int3(p, 0));
        return;
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25f * gFxaaBias,
        gFxaaBiasMin);
    const float rcpDir = 1.0f / (min(abs(dir.x), abs(dir.y)) + reduce);

    dir = clamp(dir * rcpDir, -gFxaaSpanMax.xx, gFxaaSpanMax.xx) * texel;

    const float3 rgbA = 0.5f * (
        SampleAt(uv + dir * (1.0f / 3.0f - 0.5f)) +
        SampleAt(uv + dir * (2.0f / 3.0f - 0.5f)));

    const float3 rgbB = rgbA * 0.5f + 0.25f * (
        SampleAt(uv + dir * -0.5f) +
        SampleAt(uv + dir *  0.5f));

    // 더 넓게 섞은 rgbB가 원래 범위를 벗어나면 과하게 흐린 것이라 rgbA로 돌아간다.
    const float lumaB = Luminance(rgbB);
    gOutput[id.xy] = float4((lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB, 1.0f);
}
)";
}

#endif
