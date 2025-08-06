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
	virtual void Attack(Entity* sender, int damage) {}
	virtual void OnRay() {};
	[[Property]]
	int m_currentHP{ 100 };
	[[Property]]
	int m_maxHP{ 100 };

	Core::Delegate<void> m_onDeathEvent;
	Core::Delegate<void> m_onDamageEvent;
};
