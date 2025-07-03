#pragma once
#include "Core.Minimal.h"
#include "Entity.h"

class EntityItem;
class EntityAsis : public Entity
{
public:
	MODULE_BEHAVIOR_BODY(EntityAsis)
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
public:
	virtual void Interact() override {}
	virtual void AddItem(EntityItem* item) { m_EntityItems.push_back(item); }
private:
	std::vector<EntityItem*> m_EntityItems;
	GameObject* asisTail{ nullptr };
	float angle = 0.f;
	float radius = 5.f;
	float timer = 0.f;
};
