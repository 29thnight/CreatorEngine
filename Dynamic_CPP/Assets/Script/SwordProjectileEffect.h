#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class SwordProjectileEffect : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SwordProjectileEffect)
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

	void Initialize();
private:
	bool beLateFrame = false; //setposion으로 정한 위치가 한프레임 늦어서 이렇게 조절
	bool OnEffect = false;
	bool isstart = false;
};
