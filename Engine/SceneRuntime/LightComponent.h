#pragma once
#include "Core.Minimal.h"
#include <mathematics/color.hpp>
#include "LightProperty.h"
#include "Component.h"
#include "IRenderable.h"
#include "SceneManager.h"
#include "Entity.h"
#include "Scene.h"
#include "DataSystem.h"

// 생명주기 함수의 정의는 LightComponent.cpp에 있다.
//
// 광원이 렌더 보유층(RenderScene::m_lightProxyMap)에 등록되면서 RenderScene.h가
// 필요해졌는데, 그 헤더는 Camera·RenderPassData·프록시 계층을 통째로 끌고 온다.
// ScriptBinder의 어느 헤더도 그것을 include하지 않는 것이 이 저장소의 규약이라
// 정의를 .cpp로 옮겼다.
class LightComponent : public meta::identity<LightComponent, Component>
{
    public:
    static consteval auto reflect()
    {
        return meta::schema<Self>(
            meta::field<&Self::m_color>,
            meta::field<&Self::m_lightIndex>,
            meta::field<&Self::m_constantAttenuation>,
            meta::field<&Self::m_linearAttenuation>,
            meta::field<&Self::m_quadraticAttenuation>,
            meta::field<&Self::m_spotLightAngle>,
            meta::field<&Self::m_intencity>,
            meta::field<&Self::m_range>,
            meta::field<&Self::m_lightType>,
            meta::field<&Self::m_lightStatus>);
    }
public:
    // CT6-d: 팩토리 분기의 강제 활성 보존
    void OnDeserialized() { SetEnabled(true); }

	LightComponent() = default;

    void OnInitialized() override;
    // 트랙 렌더: 가상 Update 오버라이드를 걷어내고 LightSystem(조밀 벡터,
    // 전용 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전,
    // 근거는 AnimatorSystem.h 상단 주석 참고). 옛 Update 본문
    // (renderScene->UpdateCommand(this) 한 줄)은 LightSystem::Update로 그대로
    // 옮겨 갔다 — 전용 진입점을 새로 만들지 않은 이유는 LightSystem.h 상단
    // 주석 참고.
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void OnUninitializing() override;

    DirectX::BoundingBox GetEditorBoundingBox() const
	{
		DirectX::BoundingBox box;
		const auto& position = m_pOwner->Transform_().position;
		box.Center = { position.x, position.y, position.z };
		box.Extents = m_editorBoundingBox.Extents;
		return box;
	}
private:
    DirectX::BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };

public:
    math::color    m_color{ 1, 1, 1, 1 };
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
