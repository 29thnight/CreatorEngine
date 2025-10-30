#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "ItemType.h"
#include "EntityBigWood.generated.h"

class EntityAsis;
class CriticalMark;
class EntityBigWood : public Entity
{
public:
   ReflectEntityBigWood
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityBigWood)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	virtual void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override;
private:
	void HitAsis(EntityAsis* asis);
private:
	double radical_inverse_base2(unsigned int n);
	Mathf::Vector3 UniformRandomUpdirection(float angle, int curindex, int maxCount);
private:
	[[Property]]
	int maxHP = 100;
	[[Property]]
	int m_logDamage = 10;

	[[Property]]
	EItemType type1 = EItemType::Fruit;
	[[Property]]
	int type1Count = 0;
	[[Property]]
	EItemType type2 = EItemType::Mushroom;
	[[Property]]
	int type2Count = 0;
	[[Property]]
	EItemType type3 = EItemType::Mineral;
	[[Property]]
	int type3Count = 0;

	[[Property]]
	float m_rewardRandomRange = 3.f;
	[[Property]]
	float m_rewardUpAngle = 60.f;
	[[Property]]
	float m_minRewardUpForce = 70.f;
	[[Property]]
	float m_maxRewardUpForce = 100.f;

	[[Property]]
	int reward = 0;
	GameObject* deadObj = nullptr;
	CriticalMark* m_criticalMark = nullptr;
};
