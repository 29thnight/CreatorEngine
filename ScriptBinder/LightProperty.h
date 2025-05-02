#pragma once
#include "Core.Mathf.h"
#include "Reflection.hpp"

constexpr int MAX_LIGHTS = 255;

enum LightType
{
    DirectionalLight,
    PointLight,
    SpotLight,
	InVaild
};

AUTO_REGISTER_ENUM(LightType)

enum LightStatus
{
    Disabled,
    Enabled,
    StaticShadows
};

AUTO_REGISTER_ENUM(LightStatus)

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
        return XMMatrixLookAtLH(m_direction * -50.f, XMVectorSet(0, 0, 0, 1), { 0, 1, 0, 0 });
    }

    Mathf::Matrix GetLightProjectionMatrix(float _near, float _far, float width = 32.f, float height = 32.f) const
    {
        switch (m_lightType)
        {
        case LightType::DirectionalLight:
            return XMMatrixOrthographicLH(width, height, _near, _far);
        case LightType::PointLight:
            return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_spotLightAngle), 1.0f, _near, _far);
        case LightType::SpotLight:
            return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_spotLightAngle), 1.0f, _near, _far);
        default:
            return XMMatrixIdentity();
        }
    }
};

cbuffer LightProperties
{
    Mathf::Vector4 m_eyePosition{};
    Mathf::Color4 m_globalAmbient{};
    Light m_lights[MAX_LIGHTS];
};

cbuffer LightCount
{
    uint32 m_lightCount{};
};

cbuffer ShadowMapConstant
{
   float m_shadowMapWidth{};
    float m_shadowMapHeight{};
    Mathf::xMatrix m_lightViewProjection[3]{};
    float m_casCadeEnd1{};
    float m_casCadeEnd2{};
    float m_casCadeEnd3{};
    float _epsilon = 0.01;
    int devideShadow = 9; //max = 9 
};
struct cameraView
{
    Mathf::xMatrix cameraView;
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
