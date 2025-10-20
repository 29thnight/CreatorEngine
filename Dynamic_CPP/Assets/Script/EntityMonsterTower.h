#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "EntityMonsterTower.generated.h"
class EntityMonsterTower : public Entity
{
public:
   ReflectEntityMonsterTower
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityMonsterTower)
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
	GameObject* towerMonster = nullptr;
	GameObject* monsterSpawnPosObj = nullptr;
	std::string posTag = "ShootTag";
	std::string normalTag = "normalModel";
	std::string breakTag = "breakModel";
	[[Property]]
	float attackRange = 10.f;
	
};
