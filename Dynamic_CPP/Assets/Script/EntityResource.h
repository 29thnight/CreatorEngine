#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "ItemType.h"
#include "EntityResource.generated.h"
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

	virtual void Attack(Entity* sender, int damage);

	[[Property]]
	int  itemCode = 0;
	EItemType itemType = EItemType::Mushroom;
};
