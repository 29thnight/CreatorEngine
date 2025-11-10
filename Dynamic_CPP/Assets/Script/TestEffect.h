#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "TestEffect.generated.h"

class CharacterControllerComponent;
class Animator;
class TestEffect : public ModuleBehavior
{
public:
   ReflectTestEffect
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TestEffect)
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

	[[Method]]
	void Move(Mathf::Vector2 dir);

	[[Property]]
	float moveSpeed = 0.25f;
	CharacterControllerComponent* controller = nullptr;
	Animator* m_animator = nullptr;
};
