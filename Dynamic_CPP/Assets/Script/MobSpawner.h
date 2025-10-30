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
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	[[Method]]
	void TestSpawn(); 
	void Spawn();

	[[Property]]
	std::vector<std::string> mobPrefabNames;
	[[Property]]
	std::vector<int> mobcounts;
	[[Property]]
	int m_eventId{ 0 };
	[[Property]]
	std::string m_runtimeTag{};
	[[Property]]
	float spawnRadius = 5.f;
	[[Property]]
	bool isBound = false;
	[[Property]]
	float spawnTimer = 4.0f; //Timer 간격마다 소환
	float respawnElapsedTime = 0.f;
	[[Property]]
	bool  isActive   = true;       //활성화된후 타이머 on 

	Mathf::Vector3 SpawnArea{};
	bool triggerOnce = true;
	[[Property]]
	bool OnlyAsis = false; //아시스에만 반응하는거면
private:
	double radical_inverse_base2(unsigned int n);
	Mathf::Vector3 UniformRandomUpdirection(float angle, int curindex, int maxCount);
};
