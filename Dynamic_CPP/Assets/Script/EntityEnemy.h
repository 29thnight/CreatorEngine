#pragma once
#include "Core.Minimal.h"
#include "Entity.h"

enum class CriticalMark
{
	P1,
	P2,
	None,
};

class EntityEnemy : public Entity
{
public:
	MODULE_BEHAVIOR_BODY(EntityEnemy)
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

	void SetCriticalMark(int playerIndex);
	CriticalMark criticalMark = CriticalMark::None;
	virtual void Attack(Entity* sender, int damage);
};
