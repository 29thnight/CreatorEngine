#pragma once
#include "Core.Minimal.h"
#include <mathematics/bounds.hpp>
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

	// ── 값 writer는 반드시 dirty를 발행한다 ──
	//
	// Scene::CommitRenderProxies는 dirtyQueue만 훑는다. 발행하지 않고 필드에
	// 값만 넣으면 LightRenderProxy가 낡은 채로 남아 화면이 그대로다 —
	// 컴파일도 되고 필드를 되읽어도 새 값이라 눈에 띄지 않는다.
	//
	// 아래 필드들은 여전히 public이라 우회할 수 있다. 리플렉션 인스펙터가
	// 그렇게 쓰고 있어(ReflectionTypedDraw.h는 값을 대입만 하고 dirty를
	// 모른다) 지금 private으로 내리면 저작 경로가 멈춘다. 그 축은 별도로
	// 닫아야 하고, 그때까지 새 코드는 이 writer들만 쓴다.
	void SetLightType(LightType type)
	{
		m_lightType = type;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

	void SetColor(const math::color& color)
	{
		m_color = color;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

	// 세기는 EnhancedLight::color.a로 실린다(EnhancedLightPacking.h).
	void SetIntensity(float intensity)
	{
		m_intencity = intensity;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

	void SetRange(float range)
	{
		m_range = range;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

	// 도 단위로 받는다 — 패킹이 radians()로 바꿔 싣는다.
	void SetSpotAngle(float degrees)
	{
		m_spotLightAngle = degrees;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

	void SetLightStatus(LightStatus status)
	{
		m_lightStatus = status;
		PublishRenderProxyDirty(ProxyDirty::Payload);
	}

    void OnInitialized() override;
    // 트랙 렌더: 가상 Update 오버라이드를 걷어내고 LightSystem(조밀 벡터,
    // 전용 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전,
    // 근거는 AnimatorSystem.h 상단 주석 참고). 렌더 프록시 갱신은 X8부터
    // LightSystem에서 직접 실행하지 않고 dirty 발행 뒤 Scene의 final commit이
    // 담당한다.
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void OnUninitializing() override;

	math::aabb GetEditorBoundingBox() const
	{
		const auto& position = m_pOwner->Transform_().GetPositionValue();
		return math::aabb{
			math::vector3{ position.x, position.y, position.z },
			m_editorBoundingBox.extents };
	}
private:
	// Editor picking용 2x2x2 unit box. 중심은 매 호출마다 owner position이다.
	math::aabb m_editorBoundingBox{
		math::vector3{}, math::vector3{ 1.0f, 1.0f, 1.0f } };

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
