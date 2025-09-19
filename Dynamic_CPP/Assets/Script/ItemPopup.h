#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemPopup.generated.h"

class ItemPopup : public ModuleBehavior
{
public:
   ReflectItemPopup
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemPopup)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override;
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

private:
	//Tween
	[[Property]]
	Mathf::Vector3	m_startPos{};
	[[Property]]
	Mathf::Vector3	m_targetOffset{ 550, 300, 300 };
	[[Property]]
	float			m_targetScale{ 600.f };
	[[Property]]
	bool			m_active{ false };
	[[Property]]
	float			m_duration{ 2.f };

private:
	// Bobbing (사인파)
	bool			m_bobbing{ false };		// 트윈 완료 후 보빙 단계
	[[Property]]
	float			m_bobTime{ 0.f };			// 보빙 진행 시간
	[[Property]]
	float			m_bobAmp0{ 10.f };        // 초기 진폭 (원하는 offset)
	[[Property]]
	float			m_bobFreq{ 1.5f };        // 헤르츠(초당 왕복 횟수). 1.5Hz 예시
	[[Property]]
	float			m_bobPhase{ 0.f };        // 위상 (필요시)
	[[Property]]
	float			m_bobDamping{ 0.f };      // 감쇠율(>0이면 점점 작아짐), 0이면 유지

private:
	float			m_startScale{ 0.f };
	float			m_elapsed{};
	int				m_enterCount{};

	class GameObject* m_popupObj{};
};
