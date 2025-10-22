#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "ItemType.h"
#include "EntityItemHeal.generated.h"

class EffectComponent;
//가까이 다가가면 플레이어 HP 회복되는 아이템 //던지기 등 로직이 없어 따로 스크립트
class EntityItemHeal : public Entity
{
public:
   ReflectEntityItemHeal
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityItemHeal)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	bool CanHeal();
	void Use();
	int GetHealAmount();
	[[Property]]
	int  itemCode = 0;
	[[Property]]
	EItemType itemType = EItemType::Flower;
	
private:
	[[Property]]
	int healAmount = 10;
	EffectComponent* m_effect = nullptr;

	bool canHeal = true; //플레이어 2명다 충돌해도 한명만 힐해야 하니까
};
