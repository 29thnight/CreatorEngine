#pragma once
#include "Core.Minimal.h"
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "SceneManager.h"
#include "GameObject.h"
#include "Scene.h"
#include "DataSystem.h"

// 생명주기 함수의 정의는 LightComponent.cpp에 있다.
//
// 광원이 렌더 보유층(RenderScene::m_lightProxyMap)에 등록되면서 RenderScene.h가
// 필요해졌는데, 그 헤더는 Camera·RenderPassData·프록시 계층을 통째로 끌고 온다.
// ScriptBinder의 어느 헤더도 그것을 include하지 않는 것이 이 저장소의 규약이라
// 정의를 .cpp로 옮겼다.
class LightComponent : public Component
{
public:
    // CT6-d: 팩토리 분기의 강제 활성 보존
    void OnDeserialized() { SetEnabled(true); }

    static consteval auto describe()
    {
        return meta::describe<LightComponent>(
            meta::base<Component>(),
            meta::member<&LightComponent::m_color>(),
            meta::member<&LightComponent::m_lightIndex>(),
            meta::member<&LightComponent::m_constantAttenuation>(),
            meta::member<&LightComponent::m_linearAttenuation>(),
            meta::member<&LightComponent::m_quadraticAttenuation>(),
            meta::member<&LightComponent::m_spotLightAngle>(),
            meta::member<&LightComponent::m_intencity>(),
            meta::member<&LightComponent::m_range>(),
            meta::member<&LightComponent::m_lightType>(),
            meta::member<&LightComponent::m_lightStatus>());
    }
	GENERATED_BODY(LightComponent)

    void Awake() override;
    void Update(float deltaSeconds) override;
    void OnDestroy() override;

    // 씬의 광원 슬롯에 저작 값을 옮겨 담는다.
    //
    // ★ 이 경로는 더 이상 렌더러가 읽지 않는다. 렌더가 보는 것은
    //   LightRenderProxy 하나이고(RenderSceneViewPlan ①), 여기 남은 것은
    //   Scene::DestroyLight가 유효 슬롯을 가리는 데 쓰는 편집기 부기다.
    void ApplyLightData(Light& light);

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
    Mathf::Color4  m_color{ 1, 1, 1, 1 };
    int m_lightIndex{ -1 };
    float m_constantAttenuation{ 1.f };
    float m_linearAttenuation{ 0.09f };
    float m_quadraticAttenuation{ 0.032f };
    float m_spotLightAngle{ 30.f };
    float m_intencity{ 1.f };
	float m_range{ 10.f };
    LightType m_lightType{ DirectionalLight };
    LightStatus m_lightStatus{ Enabled };

};
