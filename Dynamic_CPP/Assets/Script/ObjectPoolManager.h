#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ObjectPool.h"
class ObjectPool;
class ObjectPoolManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ObjectPoolManager)
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


	ObjectPool* GetNormalBulletPool() { return &normalBulletPool;}
	ObjectPool* GetSpecialBulletPool() { return &specialBulletPool; }
	ObjectPool* GetBombPool() { return &bombPool; }
	ObjectPool* GetSwordProjectile() { return &swordProjectilePool; }
private:

	ObjectPool normalBulletPool;
	ObjectPool specialBulletPool;
	ObjectPool bombPool;
	ObjectPool swordProjectilePool;
};
