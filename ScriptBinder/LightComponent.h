#pragma once
#include "Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Scene.h"
#include "DataSystem.h"
#include "LightComponent.generated.h"

class LightComponent : public Component, public RegistableEvent<LightComponent>
{
public:
    ReflectLightComponent
    [[Serializable(Inheritance:Component)]]
	GENERATED_BODY(LightComponent)

    void Awake() override
    {
		Scene* scene = GetOwner()->GetScene();
		if (scene == nullptr)
		{
			return;
		}

        if (-1 == m_lightIndex)
        {
            auto pair = scene->AddLight();
            m_lightIndex = pair.first;
            Light& light = pair.second;
            scene->CollectLightComponent(this);
            ApplyLightData(light);
        }
        else
        {
           auto& light = scene->GetLight(m_lightIndex);
           scene->CollectLightComponent(this);
           ApplyLightData(light);
        }
    }

    void Update(float deltaSeconds) override
    {
        Light& light = SceneManagers->GetActiveScene()->GetLight(m_lightIndex);
		ApplyLightData(light);
    }

	void OnDestroy() override
	{
		Scene* scene = GetOwner()->GetScene();
		if (scene != nullptr && m_pOwner->IsDestroyMark())
		{
            scene->RemoveLight(m_lightIndex);
            scene->UnCollectLightComponent(this);
		}
	}

    void ApplyLightData(Light& light)
    {
        light.m_position = m_pOwner->m_transform.GetWorldPosition();
        light.m_direction = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), m_pOwner->m_transform.GetWorldQuaternion());
        light.m_direction.Normalize();
        m_direction = light.m_direction;
        light.m_color = m_color * m_intencity;
        light.m_constantAttenuation = m_constantAttenuation;
        light.m_linearAttenuation = m_linearAttenuation;
        light.m_quadraticAttenuation = m_quadraticAttenuation;
        light.m_spotLightAngle = XMConvertToRadians(m_spotLightAngle);
        light.m_lightType = static_cast<int>(m_lightType);
        light.m_lightStatus = static_cast<int>(m_lightStatus);
        light.m_range = m_range;
        light.m_intencity = m_intencity;
    }

    DirectX::BoundingBox GetEditorBoundingBox() const
	{
		DirectX::BoundingBox box;
		box.Center = Mathf::Vector3(m_pOwner->m_transform.position);
		box.Extents = m_editorBoundingBox.Extents;
		return box;
	}
private:
    BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };

public:
    Mathf::Vector4 m_position{};
    Mathf::Vector4 m_direction{ -1, -1, 1, 0 };
    [[Property]]
    Mathf::Color4  m_color{ 1, 1, 1, 1 };
    [[Property]]
    int m_lightIndex{ -1 };
    [[Property]]
    float m_constantAttenuation{ 1.f };
    [[Property]]
    float m_linearAttenuation{ 0.09f };
    [[Property]]
    float m_quadraticAttenuation{ 0.032f };
    [[Property]]
    float m_spotLightAngle{ 30.f };
    [[Property]]
    float m_intencity{ 1.f };
	[[Property]]
	float m_range{ 10.f };
    [[Property]]
    LightType m_lightType{ DirectionalLight };
    [[Property]]
    LightStatus m_lightStatus{ Enabled };

};
