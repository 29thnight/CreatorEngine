#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "IAwakable.h"
#include "IOnDisable.h"
#include "IUpdatable.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Scene.h"
#include "DataSystem.h"
#include "LightComponent.generated.h"

class LightComponent : public Component, public IRenderable, public IUpdatable, public IAwakable, public IOnDisable
{
public:
   ReflectLightComponent
    [[Serializable(Inheritance:Component)]]
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
		//m_lightIndex = scene->AddLightCount();
		//Light& light = scene->GetLightProperties().m_lights[m_lightIndex];
        auto pair = scene->AddLight();
		m_lightIndex = pair.first;
        Light& light = pair.second;

		light.m_position = m_pOwner->m_transform.position;
		light.m_direction = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), m_pOwner->m_transform.rotation);
        light.m_direction.Normalize();
        m_direction = light.m_direction;
		light.m_color = m_color * m_intencity;
		light.m_constantAttenuation = m_constantAttenuation;
		light.m_linearAttenuation = m_linearAttenuation;
		light.m_quadraticAttenuation = m_quadraticAttenuation;
		light.m_spotLightAngle = XMConvertToRadians(m_spotLightAngle);
		light.m_lightType = static_cast<int>(m_lightType);
		light.m_lightStatus = static_cast<int>(m_lightStatus);
		light.m_intencity = m_intencity;
    }

    void Update(float deltaSeconds) override
    {
        Light& light = SceneManagers->GetActiveScene()->GetLight(m_lightIndex);
        light.m_position = m_pOwner->m_transform.position;
		light.m_direction = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), m_pOwner->m_transform.rotation);
		light.m_direction.Normalize();
		m_direction = light.m_direction;
        light.m_color = m_color * m_intencity;
        light.m_constantAttenuation = m_constantAttenuation;
        light.m_linearAttenuation = m_linearAttenuation;
        light.m_quadraticAttenuation = m_quadraticAttenuation;
        light.m_spotLightAngle = XMConvertToRadians(m_spotLightAngle);
        light.m_lightType = static_cast<int>(m_lightType);
        light.m_lightStatus = static_cast<int>(m_lightStatus);
        light.m_intencity = m_intencity;
    }

	void OnDisable() override
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (scene != nullptr && m_pOwner->IsDestroyMark())
		{
            scene->RemoveLight(m_lightIndex);
		}
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
