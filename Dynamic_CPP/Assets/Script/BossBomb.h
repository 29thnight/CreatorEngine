#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "BossBomb.generated.h"

class MeshRenderer;
class BossBomb : public ModuleBehavior
{
public:
   ReflectBossBomb
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(BossBomb)
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

public:
	[[Property]]
	float maxTime{ 1.f };

	// vs
	[[Property]]
	float maxScale{};
	[[Property]]
	float scaleFrequency{};
	[[Property]]
	float rotFrequency{};

	// ps
	[[Property]]
	float flashFrequency{};

	[[Property]]
	float timeScale = 1.f;

	[[Property]]
	float explosionRadius = 3.f;
	[[Property]]
	float explosionDamage = 10.f;

public:
	void Initialize();
	void ShaderUpdate();
	void Explosion();
private:
	float currentTime = 0.f;
	bool isExplosion = false;
	std::vector<MeshRenderer*> meshRenderers;

	UINT layermask = 1 << 5;

};
