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
	[[Method]]
	void PurshaseButton();
	[[Method]]
	void ReleaseKey();

public:
	//��ư Ȧ�� ���� �̹��� ������Ʈ ������Ʈ ��
	[[Property]]
	float2 centerUV = { 0.5f, 0.5f };
	[[Property]]
	float radiusUV = 1.f;
	[[Property]]
	float percent = 1.f;
	[[Property]]
	float startAngle = 9.8f;
	[[Property]]
	int clockwise = 1;
	[[Property]]
	float featherAngle = 0.5f;
	[[Property]]
	float4 tint = { 1.f, 1.f, 1.f, 1.f };

private:
	class ItemManager* m_itemManager{ nullptr };
	class RectTransformComponent* m_rect{ nullptr };
	class RectTransformComponent* m_iconRect{ nullptr };
	class GameObject* m_iconObj{ nullptr }; // ItemUIIcon ������Ʈ
	class ItemUIIcon* m_icon{ nullptr }; // ������ ������Ʈ
	class ImageComponent* m_image{ nullptr }; // ���� �˾� ��� �̹���
	class ImageComponent* m_button{ nullptr }; // ��ư �̹���
	class ImageComponent* m_purchase{}; // ���� UI �̹���
	class TextComponent* m_descComp{ nullptr }; // ���� �ؽ�Ʈ
	class TextComponent* m_nameComp{ nullptr }; // �̸� �ؽ�Ʈ

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
	[[Property]]
	float			m_requiredSelectHold{ 0.5f };
	bool			m_isSelectComplete{ false };
	float			m_popupElapsed{ 0.f };
	float           m_lastDelta{};
	float           m_selectHold{};
	bool			m_prevPopupActive{ false };

};
