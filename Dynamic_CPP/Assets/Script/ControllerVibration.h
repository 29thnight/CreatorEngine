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


	[[Property]]
	float PlayerChargeEndPower = 0.25f;
	[[Property]]
	float PlayerChargeEndTime = 0.1f;


	//주인만
	[[Property]]
	float  PlayerChargeHitPower = 0.5f;
	[[Property]]
	float  PlayerChargeHitTime = 0.2f;

	//주인만
	[[Property]]
	float  BombExplosionPower = 0.5f;
	[[Property]]
	float  BombExplosionTime = 0.2f;

	//막타친 주인만
	[[Property]]
	float  EleteKillPower = 0.25f;
	[[Property]]
	float  EleteKillTime = 0.1f;

	//둘다
	[[Property]]
	float  GateDestroyPower = 0.5f;
	[[Property]]
	float  GateDestroyTime = 0.2f;

	[[Property]]
	float  BossKillPower = 0.75f;
	[[Property]]
	float  BossKillTime = 0.3f;

	[[Property]]
	float PlayerAllStunPower = 0.75f;
	[[Property]]
	float PlayerAllStunTime = 0.3f;

	[[Property]]
	float KoriStunPower = 0.5f;
	[[Property]]
	float KoriStunTime = 0.2f;
};
