#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Scene.h"

class LightComponent : public Component, public IRenderable
{
public:
	GENERATED_BODY(LightComponent)

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
        Light& light = SceneManagers->GetActiveScene()->GetLightProperties().m_lights[m_lightIndex];
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

    ReflectionFieldInheritance(LightComponent, Component)
	{
		PropertyField
		({
			meta_property(m_lightIndex)
			meta_property(m_position)
			meta_property(m_direction)
			meta_property(m_color)
			meta_property(m_constantAttenuation)
			meta_property(m_linearAttenuation)
			meta_property(m_quadraticAttenuation)
			meta_property(m_spotLightAngle)
			meta_property(m_lightType)
			meta_property(m_lightStatus)
			meta_property(m_intencity)
		});

		MethodField
		({
			meta_method(UpdateLight)
		});

		FieldEnd(LightComponent, PropertyAndMethodInheritance)
	}

private:
    bool m_IsEnabled{ false };
};
