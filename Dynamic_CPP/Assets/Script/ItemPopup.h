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
	// Bobbing (������)
	bool			m_bobbing{ false };		// Ʈ�� �Ϸ� �� ���� �ܰ�
	[[Property]]
	float			m_bobTime{ 0.f };			// ���� ���� �ð�
	[[Property]]
	float			m_bobAmp0{ 10.f };        // �ʱ� ���� (���ϴ� offset)
	[[Property]]
	float			m_bobFreq{ 1.5f };        // �츣��(�ʴ� �պ� Ƚ��). 1.5Hz ����
	[[Property]]
	float			m_bobPhase{ 0.f };        // ���� (�ʿ��)
	[[Property]]
	float			m_bobDamping{ 0.f };      // ������(>0�̸� ���� �۾���), 0�̸� ����

private:
	float			m_startScale{ 0.f };
	float			m_elapsed{};
	int				m_enterCount{};

	class GameObject* m_popupObj{};
};
