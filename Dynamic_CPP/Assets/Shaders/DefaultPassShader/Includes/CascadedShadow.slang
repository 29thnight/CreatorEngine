#pragma once

// 캐스케이드 그림자 표본 — Deferred · ForwardShade · MaterialGraphSceneHost 의
// 공용 식이다(EnhancedShadowPass 가 맵과 상수를 만든다).
//
// 예전에는 세 셰이더가 같은 식을 한 벌씩 들고 손으로 맞췄다. 디버그 보기를
// 그중 한 곳에만 넣으면 나머지 표면에서 화면이 거짓말을 하므로 여기 하나로
// 모은다. 상수 버퍼의 이름·배치는 소비자마다 다르므로 각자
// CascadedShadowView 를 채워 넘긴다.

#define CASCADED_SHADOW_COUNT 3u

// EnhancedShadowPass::kShadowMapSize 와 같아야 한다.
#define CASCADED_SHADOW_MAP_SIZE 2048.0

// EnhancedShadowDebugView 와 번호가 같아야 한다.
#define CASCADED_SHADOW_DEBUG_OFF         0u
#define CASCADED_SHADOW_DEBUG_CASCADE     1u
#define CASCADED_SHADOW_DEBUG_TEXEL_GRID  2u
#define CASCADED_SHADOW_DEBUG_SHADOW_ONLY 3u
#define CASCADED_SHADOW_DEBUG_DEPTH_DELTA 4u
#define CASCADED_SHADOW_DEBUG_MISSING     5u
#define CASCADED_SHADOW_DEBUG_CONTRAST    6u

// EnhancedShadowFilter 와 번호가 같아야 한다. 0 은 예전 그대로의 한 표본이다 —
// 상수를 손으로 채우는 검사(cameraForward.w = 0)가 옛 결과를 그대로 받는다.
#define CASCADED_SHADOW_FILTER_HARDWARE 0u
#define CASCADED_SHADOW_FILTER_TENT3    1u
#define CASCADED_SHADOW_FILTER_TENT5    2u
#define CASCADED_SHADOW_FILTER_TENT7    3u

struct CascadedShadowView
{
    float4x4 lightViewProjection[CASCADED_SHADOW_COUNT];
    float3   eye;
    float4   cameraForward; // xyz = 카메라 정면 · w = 디버그 보기(0~3비트) + 16 x 가장자리 필터
    float4   splits;        // xyz = 각 캐스케이드가 끝나는 뷰 깊이 · w = 고른 방향광 번호 + 1
    float4   bias;          // xyz = 캐스케이드별 깊이 편향 · w = 경사 계수
    float    blendBand;     // 경계 블렌딩 폭 · 0이면 하드 스위치
};

uint CascadedShadowDebugView(CascadedShadowView view)
{
    return uint(view.cameraForward.w) & 15u;
}

uint CascadedShadowFilter(CascadedShadowView view)
{
    return min((uint(view.cameraForward.w) >> 4u) & 3u, CASCADED_SHADOW_FILTER_TENT7);
}

// 그림자를 드리우는 방향광의 배열 번호. 0(옛 단독 검사)은 첫 광원이다.
uint CascadedShadowOwner(CascadedShadowView view)
{
    return uint(max(view.splits.w, 1.0)) - 1u;
}

float CascadedShadowViewDepth(CascadedShadowView view, float3 worldPosition)
{
    return dot(worldPosition - view.eye, view.cameraForward.xyz);
}

uint CascadedShadowCascadeAt(CascadedShadowView view, float viewDepth)
{
    uint cascade = 0u;
    if (viewDepth > view.splits.x) cascade = 1u;
    if (viewDepth > view.splits.y) cascade = 2u;
    return cascade;
}

// tan(경사각) = sqrt(1-cos^2)/cos. 수평에 가까울수록 폭주하므로 8로 자른다.
float CascadedShadowSlopeTan(float3 normal, float3 toLight)
{
    const float ndotl = saturate(dot(normal, toLight));
    return min(sqrt(max(1.0 - ndotl * ndotl, 0.0)) / max(ndotl, 1e-3), 8.0);
}

float3 CascadedShadowProject(CascadedShadowView view, uint cascade, float3 worldPosition)
{
    const float4 lightSpace = mul(float4(worldPosition, 1.0), view.lightViewProjection[cascade]);
    return lightSpace.xyz / lightSpace.w;
}

// 라이트 상자 밖은 그림자 정보가 없다.
bool CascadedShadowInside(float3 projected)
{
    return !(any(abs(projected.xy) > 1.0) || projected.z > 1.0);
}

float2 CascadedShadowUv(float3 projected)
{
    return float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
}

// 깊이 편향 = 캐스케이드 편향 x (1 + 경사 계수 x tan).
float CascadedShadowBias(CascadedShadowView view, uint cascade, float slopeTan)
{
    return view.bias[cascade] * (1.0 + view.bias.w * slopeTan);
}

// ── 가장자리 필터 ────────────────────────────────────────────────────
//
// 쌍선형 비교 표본(하드웨어 2x2) 여럿에 가중치를 주어 너비 3·5·7 텍셀의 텐트
// 필터를 만든다. 표본 수는 4·9·16 이다. 식은 The Witness 의 것을 MJP 의
// Shadows 예제(SampleShadowMapOptimizedPCF)가 옮긴 그대로다.
//
// 가중치 합은 1, 음수 없음, 표본점이 텍셀 경계를 넘을 때 연속이다 — 셋 다
// 셰이더 식을 그대로 옮긴 계산으로 검산했다. 넓을수록 지글거림과 계단 경계가
// 줄고, 반그림자가 텍셀 단위로 넓어진다(먼 캐스케이드일수록 월드 폭이 크다).

float CascadedShadowTap(Texture2DArray<float> map, SamplerComparisonState comparison,
    float2 baseUv, float u, float v, uint cascade, float depth)
{
    const float2 uv = baseUv + float2(u, v) / CASCADED_SHADOW_MAP_SIZE;
    return map.SampleCmpLevelZero(comparison, float3(uv, float(cascade)), depth);
}

float CascadedShadowFiltered(Texture2DArray<float> map, SamplerComparisonState comparison,
    uint filterMode, float2 uv, uint cascade, float depth)
{
    if (filterMode == CASCADED_SHADOW_FILTER_HARDWARE)
    {
        return map.SampleCmpLevelZero(comparison, float3(uv, float(cascade)), depth);
    }

    const float2 texel = uv * CASCADED_SHADOW_MAP_SIZE; // 1 = 텍셀 하나
    float2 baseTexel = floor(texel + 0.5);
    const float s = texel.x + 0.5 - baseTexel.x;
    const float t = texel.y + 0.5 - baseTexel.y;
    const float2 baseUv = (baseTexel - 0.5) / CASCADED_SHADOW_MAP_SIZE;

    float sum = 0.0;
    if (filterMode == CASCADED_SHADOW_FILTER_TENT3)
    {
        const float2 uw = float2(3.0 - 2.0 * s, 1.0 + 2.0 * s);
        const float2 u = float2((2.0 - s) / uw.x - 1.0, s / uw.y + 1.0);
        const float2 vw = float2(3.0 - 2.0 * t, 1.0 + 2.0 * t);
        const float2 v = float2((2.0 - t) / vw.x - 1.0, t / vw.y + 1.0);
        [unroll] for (uint y = 0u; y < 2u; ++y)
            [unroll] for (uint x = 0u; x < 2u; ++x)
                sum += uw[x] * vw[y] * CascadedShadowTap(map, comparison, baseUv, u[x], v[y], cascade, depth);
        return sum / 16.0;
    }
    if (filterMode == CASCADED_SHADOW_FILTER_TENT5)
    {
        const float3 uw = float3(4.0 - 3.0 * s, 7.0, 1.0 + 3.0 * s);
        const float3 u = float3((3.0 - 2.0 * s) / uw.x - 2.0, (3.0 + s) / uw.y, s / uw.z + 2.0);
        const float3 vw = float3(4.0 - 3.0 * t, 7.0, 1.0 + 3.0 * t);
        const float3 v = float3((3.0 - 2.0 * t) / vw.x - 2.0, (3.0 + t) / vw.y, t / vw.z + 2.0);
        [unroll] for (uint y = 0u; y < 3u; ++y)
            [unroll] for (uint x = 0u; x < 3u; ++x)
                sum += uw[x] * vw[y] * CascadedShadowTap(map, comparison, baseUv, u[x], v[y], cascade, depth);
        return sum / 144.0;
    }

    // 7x7 의 가중치는 넷 다 음수지만 곱은 양수다.
    const float4 uw = float4(5.0 * s - 6.0, 11.0 * s - 28.0, -(11.0 * s + 17.0), -(5.0 * s + 1.0));
    const float4 u = float4((4.0 * s - 5.0) / uw.x - 3.0, (4.0 * s - 16.0) / uw.y - 1.0,
        -(7.0 * s + 5.0) / uw.z + 1.0, -s / uw.w + 3.0);
    const float4 vw = float4(5.0 * t - 6.0, 11.0 * t - 28.0, -(11.0 * t + 17.0), -(5.0 * t + 1.0));
    const float4 v = float4((4.0 * t - 5.0) / vw.x - 3.0, (4.0 * t - 16.0) / vw.y - 1.0,
        -(7.0 * t + 5.0) / vw.z + 1.0, -t / vw.w + 3.0);
    [unroll] for (uint y = 0u; y < 4u; ++y)
        [unroll] for (uint x = 0u; x < 4u; ++x)
            sum += uw[x] * vw[y] * CascadedShadowTap(map, comparison, baseUv, u[x], v[y], cascade, depth);
    return sum / 2704.0;
}

// 이 캐스케이드에서 그림자 맵 텍셀 하나가 덮는 월드 폭. 정사영의 x 배율이
// 1/반지름이므로 셋째가 아니라 첫째 열의 길이를 쓴다.
float CascadedShadowWorldTexel(CascadedShadowView view, uint cascade)
{
    const float4x4 m = view.lightViewProjection[cascade];
    return 2.0 / (CASCADED_SHADOW_MAP_SIZE * max(length(float3(m[0][0], m[1][0], m[2][0])), 1e-8));
}

// 실제로 비교하는 자리. 넓은 필터는 표본점에서 더 먼 텍셀까지 읽는데, 경사면
// 에서는 그 텍셀의 면 깊이가 표본점보다 낮아 자기 면에 가린다(여드름).
//
// 경사 편향 기본값(0.5 텍셀 x (1 + 2 x tan))은 하드웨어 2x2 가 닿는 1 텍셀을
// 덮도록 맞춰져 있다. 필터가 더 닿는 거리 d(3x3 1 · 5x5 2 · 7x7 3 텍셀)는
// 표본점을 법선 쪽으로 d x 텍셀 x sin 만큼 띄워 덮는다. 평면에서는 깊이 편향
// d x tan 과 같은 몫이지만, 키 큰 가림막에 닿은 접지 그림자는 떼어 내지 않는다.
// 하드웨어 2x2 에서는 띄우지 않으므로 예전 결과와 같다.
float3 CascadedShadowSamplePosition(CascadedShadowView view, uint cascade, float3 worldPosition,
    float3 normal, float3 toLight)
{
    const float reach = float(CascadedShadowFilter(view)); // 하드웨어 2x2 너머로 더 닿는 텍셀
    if (reach <= 0.0) return worldPosition;
    const float ndotl = saturate(dot(normal, toLight));
    const float sine = sqrt(max(1.0 - ndotl * ndotl, 0.0));
    return worldPosition + normal * (reach * CascadedShadowWorldTexel(view, cascade) * sine);
}

float CascadedShadowSampleCascade(Texture2DArray<float> map, SamplerComparisonState comparison,
    CascadedShadowView view, uint cascade, float3 worldPosition, float3 normal, float3 toLight,
    float slopeTan)
{
    const float3 projected = CascadedShadowProject(view, cascade,
        CascadedShadowSamplePosition(view, cascade, worldPosition, normal, toLight));

    // 가려진 것으로 치면 맵 경계에 검은 띠가 생기므로 빛을 받는 것으로 둔다.
    if (!CascadedShadowInside(projected)) return 1.0;

    return CascadedShadowFiltered(map, comparison, CascadedShadowFilter(view),
        CascadedShadowUv(projected), cascade,
        projected.z - CascadedShadowBias(view, cascade, slopeTan));
}

// 반환값 1은 빛을 받음, 0은 가려짐.
float CascadedShadowVisibility(Texture2DArray<float> map, SamplerComparisonState comparison,
    CascadedShadowView view, float3 worldPosition, float3 normal, float3 toLight)
{
    const float viewDepth = CascadedShadowViewDepth(view, worldPosition);
    if (viewDepth > view.splits.z) return 1.0;

    const uint cascade = CascadedShadowCascadeAt(view, viewDepth);
    const float slopeTan = CascadedShadowSlopeTan(normal, toLight);

    float shadow = CascadedShadowSampleCascade(map, comparison, view, cascade, worldPosition,
        normal, toLight, slopeTan);

    // 경계 블렌딩 — 해상도가 다른 두 캐스케이드가 만나는 선을 지운다.
    if (view.blendBand > 0.0 && cascade < CASCADED_SHADOW_COUNT - 1u)
    {
        const float split = (cascade == 0u) ? view.splits.x : view.splits.y;
        const float bandStart = split - abs(split) * view.blendBand;
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4));
            shadow = lerp(shadow, CascadedShadowSampleCascade(map, comparison, view,
                cascade + 1u, worldPosition, normal, toLight, slopeTan), blend);
        }
    }

    return shadow;
}

// ── 디버그 보기 ──────────────────────────────────────────────────────
//
// 결과는 HDR 조명 타깃에 들어가 후처리(노출·톤 매핑)를 거친다. 색은 그래도
// 갈려 보이도록 고르되, 정확한 색을 보려면 톤 매핑을 끈다.

float CascadedShadowShade(float3 lit)
{
    const float luminance = dot(lit, float3(0.2126, 0.7152, 0.0722));
    return luminance / (1.0 + luminance);
}

float3 CascadedShadowCascadeTint(uint cascade)
{
    if (cascade == 0u) return float3(1.0, 0.25, 0.25);
    if (cascade == 1u) return float3(0.25, 1.0, 0.25);
    return float3(0.3, 0.45, 1.0);
}

// lit      — 이 픽셀의 원래 결과(그림자를 곱한 뒤).
// selected — 그림자를 드리우는 방향광이 그림자 없이 줄 기여.
float3 CascadedShadowDebugColor(Texture2DArray<float> map, SamplerComparisonState comparison,
    CascadedShadowView view, uint mode, float3 worldPosition, float3 normal, float3 toLight,
    float3 lit, float3 selected)
{
    const float viewDepth = CascadedShadowViewDepth(view, worldPosition);
    const bool beyond = viewDepth > view.splits.z;
    const uint cascade = CascadedShadowCascadeAt(view, viewDepth);
    const float3 projected = CascadedShadowProject(view, cascade, worldPosition);
    const bool inside = CascadedShadowInside(projected);
    const float slopeTan = CascadedShadowSlopeTan(normal, toLight);
    const float shade = 0.3 + 0.7 * CascadedShadowShade(lit);
    const float3 beyondColor = float3(0.45, 0.45, 0.5);
    const float3 outsideColor = float3(1.0, 0.0, 1.0);

    if (mode == CASCADED_SHADOW_DEBUG_SHADOW_ONLY)
    {
        return float3(CascadedShadowVisibility(map, comparison, view, worldPosition, normal, toLight));
    }

    if (mode == CASCADED_SHADOW_DEBUG_CONTRAST)
    {
        // 그림자가 이 픽셀을 최대 몇 할 어둡게 할 수 있는가. 그림자는 고른
        // 방향광의 직접광만 지우므로, 환경광이 태양을 압도하면 완전히
        // 가려도 거의 안 보인다.
        //
        // 비 = 고른 광원의 기여 / 그림자가 없을 때의 결과. 파랑(0)이면 완전히
        // 가려도 안 보이고, 노랑(1)이면 가려진 자리가 검게 된다.
        const float3 luminance = float3(0.2126, 0.7152, 0.0722);
        const float visibility = CascadedShadowVisibility(map, comparison, view, worldPosition, normal, toLight);
        const float3 unshadowed = lit + selected * (1.0 - visibility);
        const float ratio = saturate(dot(selected, luminance) / max(dot(unshadowed, luminance), 1e-6));
        return lerp(float3(0.05, 0.05, 0.45), float3(1.0, 0.9, 0.2), ratio);
    }

    if (beyond) return beyondColor * shade;
    if (!inside) return outsideColor * shade;

    if (mode == CASCADED_SHADOW_DEBUG_MISSING)
    {
        return float3(shade, shade, shade) * 0.6;
    }

    float3 tint = CascadedShadowCascadeTint(cascade);
    if (view.blendBand > 0.0 && cascade < CASCADED_SHADOW_COUNT - 1u)
    {
        const float split = (cascade == 0u) ? view.splits.x : view.splits.y;
        const float bandStart = split - abs(split) * view.blendBand;
        if (viewDepth > bandStart)
        {
            const float blend = saturate((viewDepth - bandStart) / max(split - bandStart, 1e-4));
            tint = lerp(tint, CascadedShadowCascadeTint(cascade + 1u), blend);
        }
    }

    if (mode == CASCADED_SHADOW_DEBUG_CASCADE)
    {
        return tint * shade;
    }

    const float2 texel = CascadedShadowUv(projected) * CASCADED_SHADOW_MAP_SIZE;

    if (mode == CASCADED_SHADOW_DEBUG_TEXEL_GRID)
    {
        const int2 cell = int2(floor(texel));
        const bool odd = ((cell.x + cell.y) & 1) != 0;
        return tint * (odd ? 1.0 : 0.35) * shade;
    }

    if (mode == CASCADED_SHADOW_DEBUG_DEPTH_DELTA)
    {
        // 깊이 차를 월드 단위로 옮긴다. mul(v, M)의 z는 M의 셋째 열과의 내적이라
        // 그 열의 xyz 길이가 1/깊이 폭이다.
        // 넓은 필터는 법선 쪽으로 띄운 자리를 비교하므로 그 자리로 잰다.
        const float4x4 m = view.lightViewProjection[cascade];
        const float depthPerWorld = max(length(float3(m[0][2], m[1][2], m[2][2])), 1e-8);
        const float3 sampled = CascadedShadowProject(view, cascade,
            CascadedShadowSamplePosition(view, cascade, worldPosition, normal, toLight));
        const float2 sampledTexel = CascadedShadowUv(sampled) * CASCADED_SHADOW_MAP_SIZE;
        const int2 cell = clamp(int2(sampledTexel), int2(0, 0), int2(int(CASCADED_SHADOW_MAP_SIZE) - 1, int(CASCADED_SHADOW_MAP_SIZE) - 1));
        const float occluder = map.Load(int4(cell, int(cascade), 0));
        const float delta = (sampled.z - occluder) / depthPerWorld;
        const float biasWorld = CascadedShadowBias(view, cascade, slopeTan) / depthPerWorld;

        // 빨강 = 편향을 넘어 가려짐 · 노랑 = 편향이 살린 자리(편향이 없으면
        // 자기 그림자, 크면 접지 그림자가 떨어져 나간다) · 초록 = 빛을 받음.
        if (delta > biasWorld) return float3(0.9, 0.12, 0.08) * shade;
        if (delta > 0.0) return float3(1.0, 0.85, 0.1) * shade;
        return float3(0.1, 0.6, 0.2) * shade;
    }

    return lit;
}
