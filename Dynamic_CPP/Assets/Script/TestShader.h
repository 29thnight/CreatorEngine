#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "TestShader.generated.h"

class TestShader : public ModuleBehavior
{
public:
   ReflectTestShader
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TestShader)
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
	float lerpValue{};
	[[Property]]
	float maxScale{};
	[[Property]]
	float scaleFrequency{};
	[[Property]]
	float rotFrequency{};

	[[Property]]
	float flashStrength{};
	[[Property]]
	float flashFrequency{};

	float t = 0.f;
	[[Property]]
	float timeScale = 1.f;
};
