#pragma once

#include "Radiance.slang"

// 표준 PBR BRDF 정본. 직접광과 IBL 이 같은 파일에서 항을 가져간다.
//
// ★ 직접광과 IBL 은 Smith 의 k 규약이 다르다 ─ 직접광은 (a+1)^2/8,
//   IBL 은 a^2/2 다. 섞으면 반사가 어둡거나 과하게 밝아지는데, 두 값이
//   같은 범위에 있어서 "조금 이상한" 정도로만 나타나 원인을 짚기 어렵다.
//   그래서 이름에 규약을 박아 둔다.

// Trowbridge-Reitz(GGX) 법선 분포. alpha = roughness^2.
float DistributionGGX(float ndoth, float alpha)
{
    const float a2 = alpha * alpha;
    const float d  = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

// 높이 상관(height-correlated) Smith 가시성. 분모의 4*NdotV*NdotL 까지
// 흡수한 형태라 호출부가 다시 나누지 않는다. 직접광 전용이다.
float VisibilitySmithHeightCorrelated(float ndotv, float ndotl, float alpha)
{
    const float a2 = alpha * alpha;
    const float v  = ndotl * sqrt(ndotv * ndotv * (1.0 - a2) + a2);
    const float l  = ndotv * sqrt(ndotl * ndotl * (1.0 - a2) + a2);
    return 0.5 / max(v + l, 1e-6);
}

// pow 대신 곱셈 사슬. cos 는 이미 saturate 된 값으로 들어온다.
float3 FresnelSchlick(float3 f0, float cosTheta)
{
    const float x  = saturate(1.0 - cosTheta);
    const float x2 = x * x;
    return f0 + (1.0 - f0) * (x2 * x2 * x);
}

// 앰비언트용 거칠기 인식 프레넬.
//
// ★ 앰비언트에 직접광과 같은 FresnelSchlick 을 쓰면 (1-cos)^5 가 1 까지
//   올라가, 거친 표면의 스치는 각에서 림이 비현실적으로 밝아진다. 상한을
//   max(1-roughness, f0) 로 눌러 금속의 유색 응답은 남기고 그 과잉만 막는다.
float3 FresnelSchlickRoughness(float3 f0, float cosTheta, float roughness)
{
    const float3 oneMinusRoughness = max(float3(1.0 - roughness), f0);
    const float  x  = saturate(1.0 - cosTheta);
    const float  x2 = x * x;
    return f0 + (oneMinusRoughness - f0) * (x2 * x2 * x);
}

// IBL 용 Smith. k = a^2/2 (Karis 2013). BRDF LUT 를 굽는 쪽과 같아야 한다.
float GeometrySchlickGgxIbl(float ndotx, float roughness)
{
    const float k = (roughness * roughness) * 0.5;
    return ndotx / (ndotx * (1.0 - k) + k);
}

float GeometrySmithIbl(float ndotv, float ndotl, float roughness)
{
    return GeometrySchlickGgxIbl(ndotv, roughness)
         * GeometrySchlickGgxIbl(ndotl, roughness);
}

// 직접광 스페큘러의 다중산란 에너지 보상(Turquin 2019).
//
// ★ GGX 단일산란은 거칠수록 에너지를 잃는다. 되돌리지 않으면 거친 금속이
//   어두워지고, 그 어두움이 알베도 탓처럼 보여 저작에서 색을 올리게 만든다.
//   dfg 는 BRDF LUT 의 (scale, bias) 로 픽셀당 이미 한 번 읽은 값이다.
float3 SpecularEnergyCompensation(float3 f0, float2 dfg)
{
    return 1.0 + f0 * (1.0 / max(dfg.x + dfg.y, 1e-4) - 1.0);
}
