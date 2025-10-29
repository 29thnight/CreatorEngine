#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "ItemType.h"
#include "EntityResource.generated.h"
class CriticalMark;
class EffectComponent;
class EntityResource : public Entity
{
public:
   ReflectEntityResource
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityResource)
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

	virtual void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override;

	[[Property]]
	EItemType itemType = EItemType::Mushroom;
	[[Property]]
	int maxHP = 1;



private:
	EffectComponent* m_effect = nullptr;
	CriticalMark* m_criticalMark;
	[[Property]]
	float m_minRewardUpForce = 2.f;
	[[Property]]
	float m_maxRewardUpForce = 3.f;
	GameObject* deadObj = nullptr;
};
