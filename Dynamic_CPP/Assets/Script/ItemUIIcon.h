#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemUIIcon.generated.h"

class ItemUIIcon : public ModuleBehavior
{
public:
   ReflectItemUIIcon
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemUIIcon)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	bool IsPopupComplete() const { return m_isPopupComplete; }
	bool IsSetPopup() const { return m_isSetPopup; }

private:
	class RectTransformComponent* m_rect = nullptr;
	class GameObject* m_target = nullptr;

	enum class PopupPhase { None, ToPopup, ToScreen }; // 왕복 트윈 상태

private:
	[[Property]]
	bool			m_isSetPopup = false; // 보빙 멈추고 팝업으로 전환
	[[Property]]
	int				itemID{};
	[[Property]]
	Mathf::Vector2	screenOffset{};
	[[Property]]
	Mathf::Vector2	popupOffset{};
	[[Property]]
	float			m_duration{ 2.f };
	Mathf::Vector3	m_startPos{};
	bool            m_isPopupComplete{ false };
private:
	// Bobbing (사인파)
	[[Property]]
	bool			m_bobbing{ true };		// 보빙 단계
	[[Property]]
	float			m_bobTime{ 0.f };		// 보빙 진행 시간
	[[Property]]
	float			m_bobAmp0{ 7.f };        // 초기 진폭 (원하는 offset)
	[[Property]]
	float			m_bobFreq{ 0.6f };        // 헤르츠(초당 왕복 횟수). 1.5Hz 예시
	[[Property]]
	float			m_bobPhase{ 0.f };        // 위상 (필요시)
	[[Property]]
	float			m_bobDamping{ 0.f };      // 감쇠율(>0이면 점점 작아짐), 0이면 유지

private:
	float			m_elapsed{};
	float			m_popupElapsed{ 0.f };   // 팝업 진행 시간
	bool			m_popupInit{ false };    // 첫 프레임 초기화 여부
	bool            m_prevIsSetPopup{ false };
	PopupPhase		m_phase{ PopupPhase::None };
};
