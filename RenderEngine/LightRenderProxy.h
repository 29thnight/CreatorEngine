#pragma once
// 광원의 보유층 표현. 프리미티브가 그런 것처럼 라이트도 등록/해제로만
// 변하고, 렌더는 프레임 경계에서 스냅샷으로 읽는다.
//
// ★ 왜 프록시인가. 예전에는 광원만 이 흐름 밖이었다 — Scene::m_lights를
//   매 프레임 통째로 순회해 LightController로 복사하고, 카메라마다 다시
//   셰이더 구조체로 변환했다. 값이 흘러가는 자리가 셋이라 단일 진실이
//   없었고, 그 대가가 두 가지로 드러났다.
//   ① 패스마다 상한이 제각각(255 · 64 · 32 · 20)이라 "255개 지원"이
//      Deferred에서는 64개부터 조용히 거짓이 됐다.
//   ② 세기가 두 번 곱해졌다 — 컴포넌트가 색에 한 번, 라이브가 color.w에
//      한 번 실어 셰이더의 rgb*a에서 제곱이 됐다.
//
// ②는 이 프록시가 구조로 닫는다: **여기 담기는 색은 저작 색 그대로이고
// 세기는 따로 든다.** 곱은 셰이더의 rgb*a 한 번뿐이다. 변환 지점이 하나로
// 모였으므로 다음에 같은 부류가 생기려면 이 파일을 고쳐야 한다.
#include "RenderProxy.h"
#include "Interfaces/LightProperty.h"
#ifndef DYNAMICCPP_EXPORTS

class LightComponent;

class LightRenderProxy : public RenderProxy
{
public:
	// 컴포넌트에서 읽어 온 저작 값 한 벌.
	//
	// ★ 생성과 갱신이 같은 것을 읽으므로 구조체로 묶었다. 갱신 커맨드는
	//   이 값을 람다에 복사해 렌더 스레드로 보낸다 — 거기서 컴포넌트를
	//   역참조하면 그 사이 파괴된 오브젝트를 읽는다.
	struct Values
	{
		Mathf::Vector3	worldPosition{ 0.f, 0.f, 0.f };
		Mathf::Color4	color{ 1.f, 1.f, 1.f, 1.f };
		Mathf::Vector4	direction{ 0.f, 0.f, 1.f, 0.f };
		float			intensity{ 1.f };
		float			constantAttenuation{ 1.f };
		float			linearAttenuation{ 0.09f };
		float			quadraticAttenuation{ 0.032f };
		float			spotLightAngle{ 30.f };
		float			range{ 10.f };
		int				lightType{ LightType::DirectionalLight };
		int				lightStatus{ LightStatus::Enabled };
	};

	// 컴포넌트를 읽는 쪽은 게임플레이 소유다(ScriptBinder에 정의).
	static Values ReadFrom(LightComponent* component);

	LightRenderProxy() = default;
	explicit LightRenderProxy(LightComponent* component);

	void Apply(const Values& values)
	{
		m_worldPosition			= values.worldPosition;
		m_color					= values.color;
		m_direction				= values.direction;
		m_intensity				= values.intensity;
		m_constantAttenuation	= values.constantAttenuation;
		m_linearAttenuation		= values.linearAttenuation;
		m_quadraticAttenuation	= values.quadraticAttenuation;
		m_spotLightAngle		= values.spotLightAngle;
		m_range					= values.range;
		m_lightType				= values.lightType;
		m_lightStatus			= values.lightStatus;
	}

	void DestroyProxy() override;

	bool IsEnabled() const { return LightStatus::Disabled != m_lightStatus; }

	// 이 광원이 닿는 최대 거리. 뷰별 광원 목록(RenderSceneViewPlan ②)이
	// 프러스텀 교차에 쓸 값이라 여기 둔다 — 방향광은 무한대로 친다.
	bool IsInfiniteReach() const { return LightType::DirectionalLight == m_lightType; }

public:
	// 저작 값 그대로다. 세기를 색에 미리 곱하지 않는다(파일 머리 주석 ②).
	Mathf::Color4					m_color{ 1.f, 1.f, 1.f, 1.f };
	Mathf::Vector4					m_direction{ 0.f, 0.f, 1.f, 0.f };
	float							m_intensity{ 1.f };

	float							m_constantAttenuation{ 1.f };
	float							m_linearAttenuation{ 0.09f };
	float							m_quadraticAttenuation{ 0.032f };
	// 도(degree) 단위. 라디안 변환은 셰이더 구조체로 옮겨 담는 자리에서 한다.
	float							m_spotLightAngle{ 30.f };
	float							m_range{ 10.f };

	int								m_lightType{ LightType::DirectionalLight };
	int								m_lightStatus{ LightStatus::Enabled };
};
#endif // !DYNAMICCPP_EXPORTS
