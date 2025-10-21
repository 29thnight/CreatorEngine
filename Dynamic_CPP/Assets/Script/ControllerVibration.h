#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ControllerVibration.generated.h"
class ControllerVibration : public ModuleBehavior
{
public:
   ReflectControllerVibration
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ControllerVibration)
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

	[[Property]]
	float PlayerHitPower = 0.5f;
	[[Property]]
	float PlayerHitTime = 0.1f;

	[[Property]]
	float PlayerChargePower = 0.5f;
	[[Property]]
	float PlayerChargeTime = 0.1f;
	
};
