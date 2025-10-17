#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "MobSpawner.generated.h"

class MobSpawner : public ModuleBehavior
{
public:
   ReflectMobSpawner
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(MobSpawner)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void Spawn();

	[[Property]]
	std::vector<std::string> mobPrefabNames;
	[[Property]]
	std::vector<int> mobcounts;

	[[Property]]
	float spawnRadius = 5.f;

	[[Method]]
	void TestSpawn();

	Mathf::Vector3 SpawnArea{};
	std::mutex mtx;

	bool triggerOnce = true;
private:
	double radical_inverse_base2(unsigned int n);
	Mathf::Vector3 UniformRandomUpdirection(float angle, int curindex, int maxCount);
};
