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

private:
	Mathf::Vector3 m_startPos{};
	Mathf::Vector3 m_targetOffset{ 550, 300, 300 };

	float m_startScale{ 0.f };
	float m_targetScale{ 600.f };

	[[Property]]
	bool m_active{ false };
	float m_elapsed{};
	float m_duration{ 2.f };

	class GameObject* m_popupObj{};
};
