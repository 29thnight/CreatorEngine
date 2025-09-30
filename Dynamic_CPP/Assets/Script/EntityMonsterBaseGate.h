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
	int maxHP = 20;
	[[Property]]
	int halfDestroyedHP = 10;  //비율로하거나 고정값으로 하거나 이떄가되면 모델변경 or 텍스쳐변경or 애니메이션등
};
