#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemUIPopup.generated.h"

class ItemUIPopup : public ModuleBehavior
{
public:
   ReflectItemUIPopup
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemUIPopup)
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

	void SetIconObject(class GameObject* iconObj);
	void ClearIconObject() { m_iconObj = nullptr; m_icon = nullptr; }

private:
	class RectTransformComponent* m_rect{ nullptr };
	class RectTransformComponent* m_iconRect{ nullptr };
	//�� �κ��� 2���� Popup�� ��ȯ�ؼ� ó���� �Ÿ� Manager���� ������ ��� ��.
	class GameObject* m_iconObj{ nullptr }; // ItemUIIcon ������Ʈ
	class ItemUIIcon* m_icon{ nullptr };
	class ImageComponent* m_image{ nullptr };

private:
	[[Property]]
	int				itemID{};
	[[Property]]
	int				rarityID{};
	[[Property]]
	Mathf::Vector2	m_baseSize{};				// ���� ũ��
	[[Property]]
	Mathf::Vector2	m_popupSize{};				// �˾� ��ǥ ũ��
	[[Property]]
	Mathf::Vector2	m_targetSize{};				// ���� ��ǥ
	[[Property]]
	float			m_duration{ 0.3f };			// ���� �ð�
	float			m_popupElapsed{ 0.f };

	bool			m_prevPopupActive{ false };

};
