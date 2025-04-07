#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "GameObject.h"
#include "Scene.h"

class LightComponent : public Component, public IRenderable
{
public:
    LightComponent() meta_default(LightComponent)
    ~LightComponent() = default;
    std::string ToString() const override
    {
        return std::string("LightComponent");
    }
    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }
    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

    void UpdateLight()
    {
        Light& light = GameObject::m_pScene->GetLightProperties().m_lights[m_lightIndex];
        light.m_position = m_position;
        light.m_direction = m_direction;
        light.m_color = m_color;
        light.m_constantAttenuation = m_constantAttenuation;
        light.m_linearAttenuation = m_linearAttenuation;
        light.m_quadraticAttenuation = m_quadraticAttenuation;
        light.m_spotLightAngle = m_spotLightAngle;
        light.m_lightType = static_cast<int>(m_lightType);
        light.m_lightStatus = static_cast<int>(m_lightStatus);
        light.m_intencity = m_intencity;
    }

    int m_lightIndex{ 0 };

    Mathf::Vector4 m_position{};
    Mathf::Vector4 m_direction{ 0,0,1,0 };
    Mathf::Color4  m_color{};

    float m_constantAttenuation{ 1.f };
    float m_linearAttenuation{ 0.09f };
    float m_quadraticAttenuation{ 0.032f };
    float m_spotLightAngle{ 60.f };

    LightType m_lightType{ DirectionalLight };
    LightStatus m_lightStatus{ Enabled };
    float m_intencity{ 5.f };

    static const Meta::Type& Reflect()
    {
        ReflectEnum_LightType();
        ReflectEnum_LightStatus();

        static const Meta::MetaProperties<10> properties
        {
            Meta::MakeProperty("m_position", &LightComponent::m_position),
            Meta::MakeProperty("m_direction", &LightComponent::m_direction),
            Meta::MakeProperty("m_color", &LightComponent::m_color),
            Meta::MakeProperty("m_constantAttenuation", &LightComponent::m_constantAttenuation),
            Meta::MakeProperty("m_linearAttenuation", &LightComponent::m_linearAttenuation),
            Meta::MakeProperty("m_quadraticAttenuation", &LightComponent::m_quadraticAttenuation),
            Meta::MakeProperty("m_spotLightAngle", &LightComponent::m_spotLightAngle),
            Meta::MakeProperty("m_lightType", &LightComponent::m_lightType),
            Meta::MakeProperty("m_lightStatus", &LightComponent::m_lightStatus),
            Meta::MakeProperty("m_intencity", &LightComponent::m_intencity)
        };

        static const Meta::Type type
        {
            "LightComponent",
            properties
        };

        return type;
    }

private:
    bool m_IsEnabled{ false };
};
