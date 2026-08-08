#pragma once
// 보유층의 광원 프록시를 셰이더가 읽는 형태로 옮겨 담는다.
//
// ★ 함수로 뽑은 이유. 이 변환이 라이브 러너와 자가 검증 두 곳에 손으로
//   복사돼 있었고, 두 곳이 갈리면 "검증은 통과하는데 실전만 다른 그림"이
//   된다 — 그 부류는 원인을 찾기가 특히 나쁘다.
//
// ★ 세기는 여기서 곱하지 않는다. color.rgb는 저작 색 그대로, color.a가
//   세기이고 곱은 셰이더의 rgb*a 한 번뿐이다. 예전에는 컴포넌트가 색에
//   미리 곱한 값을 싣고 여기서 세기를 a에 또 실어 제곱이 됐다.
#include "EnhancedRenderPass.h"
#include "../../LightRenderProxy.h"

inline EnhancedLight MakeEnhancedLight(const LightRenderProxy& source)
{
    EnhancedLight light{};

    light.position = Mathf::Vector4{
        source.m_worldPosition.x, source.m_worldPosition.y, source.m_worldPosition.z,
        static_cast<float>(source.m_lightType) };

    light.direction = source.m_direction;
    light.direction.w = XMConvertToRadians(source.m_spotLightAngle);

    light.color = source.m_color;
    light.color.w = source.m_intensity;

    light.attenuation = Mathf::Vector4{
        source.m_constantAttenuation, source.m_linearAttenuation,
        source.m_quadraticAttenuation, source.m_range };

    return light;
}
