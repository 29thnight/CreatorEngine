cbuffer UniformBuffer : register(b0)
{
    float4 gridColor;
    float4 checkerColor;
    float fadeStart;
    float fadeEnd;
    float unitSize;
    int subdivisions;
    float3 centerOffset;
    float majorLineThickness;
    float minorLineThickness;
    float minorLineAlpha;
};
cbuffer cameraPos : register(b1)
{
    float4 cameraPos;
}
// 출력 구조체: 클립 좌표와 함께 월드 좌표를 TEXCOORD0로 전달합니다.
struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

// fwidth 대용 (파생값 절댓값 합)
static float2 fwidth2(float2 v)
{
    return abs(ddx(v)) + abs(ddy(v));
}

// 절대 좌표 기반 그리드 라인 마스크(0~1)
float grid_mask(float2 posAbs, float unit, float thickness)
{
    unit = max(unit, 1e-6); // 0 나눗셈 방지
    float2 fw = fwidth2(posAbs);
    float2 threshold = fw * thickness * 0.5 / unit;

    float2 coord = posAbs / unit;
    float2 fracP = frac(coord);
    float2 fracN = frac(-coord);

    float2 hit;
    hit.x = ((fracP.x < threshold.x) ? 1.0 : 0.0) + ((fracN.x < threshold.x) ? 1.0 : 0.0);
    hit.y = ((fracP.y < threshold.y) ? 1.0 : 0.0) + ((fracN.y < threshold.y) ? 1.0 : 0.0);

    return saturate(max(hit.x, hit.y));
}

float4 main(VS_OUTPUT input) : SV_TARGET
{
// ---- 절대 좌표로 그리드 계산 (월드 원점 + centerOffset 기준) ----
    float2 posAbs = input.worldPos.xz - centerOffset.xz;

    float fSubs = max((float) subdivisions, 1.0);
    float minorUnit = unitSize / fSubs;

    float major = grid_mask(posAbs, unitSize, majorLineThickness);
    float minor = grid_mask(posAbs, minorUnit, minorLineThickness) * minorLineAlpha;
    float lineMask = saturate(major + minor);

    // ---- 카메라 기준으로 가시 범위 페이드 (절대 좌표 패턴은 유지) ----
    float distPlanar = length(input.worldPos.xz - cameraPos.xz); // ← 절대/float 계산, int 캐스팅 금지
    float denom = max(fadeEnd - fadeStart, 1e-5);
    float fadeFactor = 1.0 - saturate((distPlanar - fadeStart) / denom);

    // 추가로 너무 멀면 살짝 더 감쇠하고 싶다면 (옵션)
     fadeFactor *= (1.0 - saturate(distPlanar / 100.0));

    // ---- 색/알파 합성 ----
    float alphaGrid = lineMask * gridColor.a;
    float alpha = saturate(alphaGrid) * fadeFactor;

    // 배경(checkerColor) 위에 라인(gridColor)만 섞기
    float3 color = lerp(checkerColor.rgb, gridColor.rgb, lineMask);

    return float4(color, alpha);
}