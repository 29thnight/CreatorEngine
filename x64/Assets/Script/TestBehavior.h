#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class TestBehavior : public ModuleBehavior
{
public:
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void OnTriggerEnter(ICollider* other);
	virtual void OnTriggerStay(ICollider* other);
	virtual void OnTriggerExit(ICollider* other);
	virtual void OnCollisionEnter(ICollider* other);
	virtual void OnCollisionStay(ICollider* other);
	virtual void OnCollisionExit(ICollider* other);
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;

	// MonoBehavior을(를) 통해 상속됨
	std::string ToString() const override
	{
		return "TestBehavior";
	};
};