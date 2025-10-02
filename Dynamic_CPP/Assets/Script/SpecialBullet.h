#pragma once
#include "Core.Minimal.h"
#include "Bullet.h"
#include "SpecialBullet.generated.h"

class SpecialBullet : public Bullet
{
public:
   ReflectSpecialBullet
	   [[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SpecialBullet)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
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
	[[Property]]
	float explosionRadius = 3.0f; //범위공격 반경
};
