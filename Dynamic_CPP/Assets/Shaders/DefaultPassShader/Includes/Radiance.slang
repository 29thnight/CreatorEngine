#pragma once

// HDR 라디언스를 다루는 공통 도구. IBL 굽기·조명·포스트가 같은 정의를 쓴다.
//
// ★ 이 파일이 생긴 이유 ─ 예전 IblIrradiance.hlsl / IblPrefilter.hlsl 은
//   파이어플라이를 `min(color, 1.2)` 로 자르고 `Luminance > 1.5` 인 표본을
//   **검정으로 버렸다**. 둘 다 되돌릴 수 없는 연산이라, 태양이 있는 HDRI 는
//   하늘이 통째로 상수판이 되고 밝은 곳에는 구멍이 뚫렸다. 그 환경으로 구운
//   프리필터를 금속이 반사하면 금이 회색 플라스틱으로 보인다.
//
//   여기 있는 FireflyTonemap/Untonemap 은 **가역**이다. 표본 하나짜리
//   (거울 반사) 에서는 정확히 항등이고, 여러 표본을 평균낼 때만 밝은 이상치의
//   지분을 낮춘다 ─ Karis 의 tone-map-then-average.

static const float PI     = 3.14159265359;
static const float INV_PI = 0.31830988618;

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// HDR 파노라마에는 +Inf/NaN 텍셀이 실제로 들어 있다. 하나라도 누적기에 닿으면
// 그 뒤 전부가 NaN 이 되고, NaN 은 clamp 를 그대로 통과해 화면에 남는다.
//
// 분기 대신 select 로 쓰는 이유: isinf/isnan 은 호출마다 분기 둘을 만들고
// 이 함수는 표본 루프 안에서 1024 번 돈다.
float SanitizeRadianceChannel(float value)
{
    const bool isFinite = !isinf(value) && !isnan(value);
    return isFinite ? clamp(value, 0.0, 64000.0) : 0.0;
}

float3 SanitizeRadiance(float3 color)
{
    return float3(SanitizeRadianceChannel(color.r),
                  SanitizeRadianceChannel(color.g),
                  SanitizeRadianceChannel(color.b));
}

// 가역 파이어플라이 억제. 누적은 톤맵 공간에서 하고 평균을 낸 뒤 되돌린다.
//
// 표본이 하나면 Untonemap(Tonemap(c)) == c 다(부동소수 오차 제외). 그래서
// 거칠기 0 의 거울 반사는 손대지 않고, 거친 다중 표본 수집만 부드러워진다.
float3 FireflyTonemap(float3 color)
{
    return color / (1.0 + Luminance(color));
}

float3 FireflyUntonemap(float3 color)
{
    return color / max(1.0 - Luminance(color), 1e-4);
}
