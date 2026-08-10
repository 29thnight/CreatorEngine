#include "PostChainCommon.hlsli"

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
