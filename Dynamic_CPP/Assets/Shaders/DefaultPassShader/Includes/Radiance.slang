#pragma once

// HDR 라디언스를 다루는 공통 도구. IBL 굽기·조명·포스트가 같은 정의를 쓴다.
//
// ★ 이 파일이 생긴 이유 ─ 예전 IblIrradiance.hlsl / IblPrefilter.hlsl 은
//   파이어플라이를 `min(color, 1.2)` 로 자르고 `Luminance > 1.5` 인 표본을
//   **검정으로 버렸다**. 둘 다 되돌릴 수 없는 연산이라, 태양이 있는 HDRI 는
//   하늘이 통째로 상수판이 되고 밝은 곳에는 구멍이 뚫렸다. 그 환경으로 구운
//   프리필터를 금속이 반사하면 금이 회색 플라스틱으로 보인다.
//
//   그 뒤로 한동안 **가역**을 자처한 FireflyTonemap/Untonemap 을 썼는데,
//   그것도 답이 아니었다. 톤맵 공간에서 누적해 평균낸 뒤 되돌리는 연산은
//   균일 환경에서만 항등이고, 대비가 있으면 어두운 쪽으로 크게 편향된다 ─
//   태양이 있는 HDRI 로 재니 조도가 참값의 0.53 배까지 떨어졌다. 누적값에
//   손대는 억제는 형태를 바꿔도 에너지를 깎는다.
//
//   환경 적분은 IblImportance.slang의 MIS로 밝은 영역을 직접 표집한다.
//   이 파일은 저장 가능한 라디언스 범위와 비유한 값의 처리만 정의한다.

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

