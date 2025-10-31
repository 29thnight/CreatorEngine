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
	virtual void OnTriggerEnter(const Collision& collision) override {};
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {};
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void SetPopup(bool popup) { m_isSetPopup = popup; }
	bool IsPopupComplete() const { return m_isPopupComplete; }
	bool IsSetPopup() const { return m_isSetPopup; }

	void SetTarget(class GameObject* target);
	void SetItemID(int id);
	void SetItemEnhancement(int id);
	void SetRarityID(int id);
	void ApplyOrderDelta(int delta);
	int GetItemID() const { return itemID; }
	int GetRarityID() const { return rarityID;  }

	void OnPurchased() { m_isPurchased = true; }
	void ResetPurchased() { m_isPurchased = false; }

	[[Property]]
	int				m_playerID{ -1 };

private:
	class ItemManager*				m_itemManager = nullptr;
	class RectTransformComponent*	m_rect = nullptr;
	class ImageComponent*			m_image = nullptr;
	class GameObject*				m_target = nullptr;
	class ItemComponent*			m_itemComp = nullptr;
	enum class PopupPhase { None, ToPopup, ToScreen }; // 왕복 트윈 상태
	class GameManager* GM = nullptr;
private:
	[[Property]]
	bool			m_isSetPopup = false; // 보빙 멈추고 팝업으로 전환
	[[Property]]
	int				itemID{};
	[[Property]]
	int				itemTypeID{};
	[[Property]]
	int				rarityID{};
	[[Property]]
	Mathf::Vector2	screenOffset{};
	[[Property]]
	Mathf::Vector2	popupOffset{};
	[[Property]]
	float			m_duration{ 2.f };
	Mathf::Vector3	m_startPos{};
	bool            m_isPopupComplete{ false };
	bool			m_isPurchased{ false };
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
	int             m_enterCount{ 0 };          // 트리거 진입 횟수 (중첩 방지용)
	PopupPhase		m_phase{ PopupPhase::None };
	bool			m_isElevated = false;
	bool			m_popupRaised = false;    // 팝업 트윈 시작 시 +10 했는지
	bool			m_dismissRaised = false;  // 해제 트윈 시작 시 +10 했는지
};
