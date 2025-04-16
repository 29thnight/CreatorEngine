#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class TestScriptClass : public ModuleBehavior
{
public:
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void OnTriggerEnter(ICollider* other) override;
	virtual void OnTriggerStay(ICollider* other) override;
	virtual void OnTriggerExit(ICollider* other) override;
	virtual void OnCollisionEnter(ICollider* other) override;
	virtual void OnCollisionStay(ICollider* other) override;
	virtual void OnCollisionExit(ICollider* other) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;
};
