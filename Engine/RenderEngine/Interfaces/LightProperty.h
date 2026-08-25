#pragma once
#include "Core.Mathf.h"
#include "Reflection.hpp"

// MAX_LIGHTS(255) · LightProperties · LightCount · cameraView를 걷었다.
//
// 넷 다 DX11 시절 상수 버퍼의 형상이다. 광원 배열을 통째로 cbuffer에 실어
// 올리던 경로(Scene::UpdateLight -> LightController -> DX11 패스)가 사라진
// 뒤로 소비자가 0이었고, 그런데도 "255개까지 지원"이라는 인상만 남아 있었다.
// 실제 한도는 소비하는 DX12 패스가 정한다 — Deferred 64 · Forward+ 타일당
// 32 · VolumetricFog 20. 그 선별을 뷰가 한 번에 하는 것이
// RenderSceneViewPlan ②다.
//
// 남긴 것: Light(편집기 부기 슬롯 · Scene::m_lights), ShadowMapConstant
// (Camera가 든다), ShadowMapRenderDesc(DX12 라이브가 쓴다).

enum LightType : uint16_t
{
    DirectionalLight,
    PointLight,
    SpotLight,
};

constexpr int LightType_InVaild = -1;


enum LightStatus : uint16_t
{
    Disabled,
    Enabled,
    StaticShadows
};


cbuffer Light
{
    Mathf::Vector4 m_position{};
    Mathf::Vector4 m_direction{ 0,0,1,0 };
    Mathf::Color4  m_color{};

    float m_constantAttenuation{ 1.f };
    float m_linearAttenuation{ 0.09f };
    float m_quadraticAttenuation{ 0.032f };
    float m_spotLightAngle{ 60.f };

    int m_lightType{};
    int m_lightStatus{};
    float m_range{ 10.f };
    float m_intencity{ 5.f };

    Mathf::Matrix GetLightViewMatrix() const
    {
        return DirectX::XMMatrixLookAtLH(
            DirectX::XMVectorScale(m_direction, -50.f),
            DirectX::XMVectorSet(0, 0, 0, 1),
            DirectX::XMVectorSet(0, 1, 0, 0));
    }

    Mathf::Matrix GetLightProjectionMatrix(float _near, float _far, float width = 32.f, float height = 32.f) const
    {
        switch (m_lightType)
        {
        case LightType::DirectionalLight:
            return DirectX::XMMatrixOrthographicLH(width, height, _near, _far);
        case LightType::PointLight:
            return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(m_spotLightAngle), 1.0f, _near, _far);
        case LightType::SpotLight:
            return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(m_spotLightAngle), 1.0f, _near, _far);
        default:
            return DirectX::XMMatrixIdentity();
        }
    }
};

cbuffer ShadowMapConstant
{
    float m_shadowMapWidth{};
    float m_shadowMapHeight{};
    Mathf::xMatrix m_lightViewProjection[3]{};
    float m_casCadeEnd1{};
    float m_casCadeEnd2{};
    float m_casCadeEnd3{};
    float _epsilon = 0.001;
    int devideShadow = 9; //max = 9 
    bool32 useCasCade;
};
struct ShadowMapRenderDesc
{
    Mathf::xVector m_eyePosition{};
    Mathf::xVector m_lookAt{};
    float m_nearPlane{ 0.1f };
    float m_farPlane{ 200.f };
    float m_viewWidth{ 1.f };
    float m_viewHeight{ 1.f };
    float m_textureWidth{ 8192.f };
    float m_textureHeight{ 8192.f };
};
