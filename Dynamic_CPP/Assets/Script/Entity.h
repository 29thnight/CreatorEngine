#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(Entity)
	virtual void Awake() override {}
	virtual void Start() override {}
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) {}
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}
public:
	virtual void Interact() {}
	virtual void SendDamage(Entity* sender, int damage) {}
	virtual void SendKnockBack(Entity* sender, Mathf::Vector2 KnockBackForce) {}
	virtual void OnRay() {};
	virtual void AttakRay() {};
	int m_currentHP{ 1 };
	int m_maxHP{ 100 };


	bool isKnockBack = false;
	float KnockBackElapsedTime = 0.f;
	float KnockBackTime = 0.f;  //넉백지속시간 //  총거리는같지만 빨리끝남

	Core::Delegate<void> m_onDeathEvent;
	Core::Delegate<void> m_onDamageEvent;
};
