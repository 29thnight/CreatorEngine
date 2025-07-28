#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "TestBehavior.generated.h"

class TestBehavior : public ModuleBehavior
{
public:
   ReflectTestBehavior
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TestBehavior)

public:
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void OnTriggerEnter(const Collision& collider) override;
	virtual void OnTriggerStay(const Collision& collider) override;
	virtual void OnTriggerExit(const Collision& collider) override;
	virtual void OnCollisionEnter(const Collision& collider) override;
	virtual void OnCollisionStay(const Collision& collider) override;
	virtual void OnCollisionExit(const Collision& collider) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;

public:
	[[Property]]
	int testValue = 0;
	[[Property]]
	std::string testString = "TestBehavior";

	float m_chargingTime = 0.f;

	Mathf::Vector2 moveDir{};
	void Move(Mathf::Vector2 value);
};
