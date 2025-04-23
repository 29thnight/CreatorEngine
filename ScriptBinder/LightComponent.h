#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "IAwakable.h"
#include "IUpdatable.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Scene.h"
#include "LightComponent.generated.h"
//어차피 다시 만들어야 하니까
class LightComponent : public Component, public IRenderable, public IUpdatable, public IAwakable
{
public:
   ReflectLightComponent
    [[Serializable]]
	GENERATED_BODY(LightComponent)

    bool IsEnabled() const override
    {
        return m_IsEnabled;
    }
    void SetEnabled(bool able) override
    {
        m_IsEnabled = able;
    }

    void Awake() override
    {
		Scene* scene = SceneManagers->GetActiveScene();
		if (scene == nullptr)
		{
			return;
		}
		m_lightIndex = scene->AddLightCount();
		Light& light = scene->GetLightProperties().m_lights[m_lightIndex];
		light.m_position = m_pOwner->m_transform.position;
		light.m_direction = m_pOwner->m_transform.rotation;
        light.m_direction.Normalize();
		light.m_color = m_color * m_intencity;
		light.m_constantAttenuation = m_constantAttenuation;
		light.m_linearAttenuation = m_linearAttenuation;
		light.m_quadraticAttenuation = m_quadraticAttenuation;
		light.m_spotLightAngle = m_spotLightAngle;
		light.m_lightType = static_cast<int>(m_lightType);
		light.m_lightStatus = static_cast<int>(m_lightStatus);
		light.m_intencity = m_intencity;
    }

    void Update(float deltaSeconds) override
    {
        Light& light = SceneManagers->GetActiveScene()->GetLightProperties().m_lights[m_lightIndex];
        light.m_position = m_pOwner->m_transform.position;
		light.m_direction = m_pOwner->m_transform.rotation;
		light.m_direction.Normalize();
        light.m_color = m_color * m_intencity;
        light.m_constantAttenuation = m_constantAttenuation;
        light.m_linearAttenuation = m_linearAttenuation;
        light.m_quadraticAttenuation = m_quadraticAttenuation;
        light.m_spotLightAngle = m_spotLightAngle;
        light.m_lightType = static_cast<int>(m_lightType);
        light.m_lightStatus = static_cast<int>(m_lightStatus);
        light.m_intencity = m_intencity;
    }

    [[Property]]
    int m_lightIndex{ 0 };

    Mathf::Vector4 m_position{};
    Mathf::Vector4 m_direction{ -1, -1, 1, 0 };
    [[Property]]
    Mathf::Color4  m_color{ 1, 0, 0, 1 };

    [[Property]]
    float m_constantAttenuation{ 1.f };
    [[Property]]
    float m_linearAttenuation{ 0.09f };
    [[Property]]
    float m_quadraticAttenuation{ 0.032f };
    [[Property]]
    float m_spotLightAngle{ 60.f };

    [[Property]]
    LightType m_lightType{ DirectionalLight };
    [[Property]]
    LightStatus m_lightStatus{ Enabled };
    [[Property]]
    float m_intencity{ 5.f };

private:
    bool m_IsEnabled{ false };
};
