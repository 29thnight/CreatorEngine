#pragma once
#include <cstdint>

// MAX_LIGHTS(255) · LightProperties · LightCount · cameraView를 걷었다.
//
// 넷 다 DX11 시절 상수 버퍼의 형상이다. 광원 배열을 통째로 cbuffer에 실어
// 올리던 경로(Scene::UpdateLight -> LightController -> DX11 패스)가 사라진
// 뒤로 소비자가 0이었고, 그런데도 "255개까지 지원"이라는 인상만 남아 있었다.
// 실제 한도는 소비하는 DX12 패스가 정한다 — Deferred 64 · Forward+ 타일당
// 32 · VolumetricFog 20. 그 선별을 뷰가 한 번에 하는 것이
// RenderSceneViewPlan ②다.
//
// Scene의 편집기 부기 슬롯은 값이 아니라 점유 여부만 필요하므로 uint8_t 슬롯으로
// 축소했다. Light/ShadowMapConstant/ShadowMapRenderDesc는 실제 소비자가 0인 DX11
// 잔재였고, 라이브 그림자 데이터는 EnhancedShadowData가 단일 소유한다.

enum LightType : uint16_t
{
    DirectionalLight,
    PointLight,
    SpotLight,
};

enum LightStatus : uint16_t
{
    Disabled,
    Enabled,
    StaticShadows
};
