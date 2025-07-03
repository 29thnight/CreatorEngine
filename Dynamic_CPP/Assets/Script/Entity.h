#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(Entity)
	virtual void Awake() override = 0;
	virtual void Start() override = 0;
	virtual void FixedUpdate(float fixedTick) override = 0;
	virtual void OnTriggerEnter(const Collision& collision) override = 0;
	virtual void OnTriggerStay(const Collision& collision) override = 0;
	virtual void OnTriggerExit(const Collision& collision) override = 0;
	virtual void OnCollisionEnter(const Collision& collision) override = 0;
	virtual void OnCollisionStay(const Collision& collision) override = 0;
	virtual void OnCollisionExit(const Collision& collision) override = 0;
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}
public:
	virtual void Interact() {}
};
