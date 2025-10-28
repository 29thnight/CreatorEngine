#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "EntityMonsterBaseGate.generated.h"
//몬스터기지 입구,출구역할
class EntityMonsterBaseGate : public Entity
{
public:
   ReflectEntityMonsterBaseGate
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityMonsterBaseGate)
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

	void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override;


private:
	[[Property]]
	int maxHP = 200;
	bool isDestroy = false;
	GameObject* normalModel = nullptr;
	GameObject* breakModel = nullptr;
	std::string normalTag = "normalModel";
	std::string breakTag = "breakModel";

	GameObject* deadObj = nullptr;
};
