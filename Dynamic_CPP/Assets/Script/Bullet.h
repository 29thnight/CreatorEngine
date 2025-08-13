#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Bullet.generated.h"


class EffectComponent;
class Bullet : public ModuleBehavior
{
public:
   ReflectBullet
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Bullet)
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
	float rangedProjSpd = 1.0f; //발사체 이동속도
	[[Property]]
	float rangedProjDist = 10.f; //발사체 최대 이동거리


	EffectComponent* m_effect = nullptr; 
};
