#include "PostChainCommon.hlsli"

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
    // 이 상수는 GLSL mat3의 열 기준 순서로 공개된 AgX 행렬이다. HLSL의
    // float3x3 생성자는 같은 나열을 행으로 채우므로 벡터를 왼쪽에 둬야
    // 원래 변환과 같다. 반대로 mul(matrix, vector)를 쓰면 행렬이 전치되어
    // 중성 회색까지 붉게 밀린다.
    float3 v = mul(max(x, 1e-10f), kAgxMat);

    v = clamp(log2(v), minEv, maxEv);
    v = (v - minEv) / (maxEv - minEv);
    v = AgxContrast(v);

    return saturate(mul(v, kAgxMatInv));
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
    //
    // v는 radius+softness에서 0(완전 검정)까지 떨어지므로 그대로 곱하면
    // 코너가 반드시 검게 죽는다. intensity로 감광의 바닥을 들어 올린다 —
    // 0.3이면 v=0인 코너도 70% 밝기를 유지한다.
    if (0 != (gFlags & kFlagVignette))
    {
        const float2 uv = (float2(id.xy) + 0.5f) / float2(gDstSize);
        const float dist = length(uv - 0.5f) * 2.0f;
        const float v = smoothstep(gVignetteRadius + gVignetteSoftness,
            gVignetteRadius - gVignetteSoftness, dist);
        color *= lerp(1.0f, v, gVignetteIntensity);
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
