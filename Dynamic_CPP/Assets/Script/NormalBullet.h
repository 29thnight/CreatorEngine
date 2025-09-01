#pragma once
#include "Core.Minimal.h"
#include "Bullet.h"
#include "ModuleBehavior.h"
#include "NormalBullet.generated.h"

class NormalBullet : public Bullet
{
public:
   ReflectNormalBullet
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(NormalBullet)
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


	[[Property]]
	float rangedProjSpd = 1.0f; //�߻�ü �̵��ӵ�
	[[Property]]
	float rangedProjDist = 10.f; //�߻�ü �ִ� �̵��Ÿ� ��? �̵��Ÿ�?
};
