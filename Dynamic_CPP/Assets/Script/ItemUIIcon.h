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
	enum class PopupPhase { None, ToPopup, ToScreen }; // �պ� Ʈ�� ����
	class GameManager* GM = nullptr;
private:
	[[Property]]
	bool			m_isSetPopup = false; // ���� ���߰� �˾����� ��ȯ
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
	// Bobbing (������)
	[[Property]]
	bool			m_bobbing{ true };		// ���� �ܰ�
	[[Property]]
	float			m_bobTime{ 0.f };		// ���� ���� �ð�
	[[Property]]
	float			m_bobAmp0{ 7.f };        // �ʱ� ���� (���ϴ� offset)
	[[Property]]
	float			m_bobFreq{ 0.6f };        // �츣��(�ʴ� �պ� Ƚ��). 1.5Hz ����
	[[Property]]
	float			m_bobPhase{ 0.f };        // ���� (�ʿ��)
	[[Property]]
	float			m_bobDamping{ 0.f };      // ������(>0�̸� ���� �۾���), 0�̸� ����

private:
	float			m_elapsed{};
	float			m_popupElapsed{ 0.f };   // �˾� ���� �ð�
	bool			m_popupInit{ false };    // ù ������ �ʱ�ȭ ����
	bool            m_prevIsSetPopup{ false };
	int             m_enterCount{ 0 };          // Ʈ���� ���� Ƚ�� (��ø ������)
	PopupPhase		m_phase{ PopupPhase::None };
	bool			m_isElevated = false;
	bool			m_popupRaised = false;    // �˾� Ʈ�� ���� �� +10 �ߴ���
	bool			m_dismissRaised = false;  // ���� Ʈ�� ���� �� +10 �ߴ���
};
