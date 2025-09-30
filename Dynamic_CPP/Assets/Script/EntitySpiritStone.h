#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "EntitySpiritStone.generated.h"

class EntitySpiritStone : public Entity
{
public:
   ReflectEntitySpiritStone
	[[ScriptReflectionField(Inheritance:Entity)]]
	MODULE_BEHAVIOR_BODY(EntitySpiritStone)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	virtual void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override;

	[[Property]]
	int m_stoneReward = 0;
};
