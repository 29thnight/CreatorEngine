#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class GameManager;
class TestEnemy : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(TestEnemy)
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


	GameObject* Owner;
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;

	[[Property]]
	float detectRange = 5.f;
	GameManager* gm = nullptr;
	float targetLostTimer = 0.f;
	float maxTargetLostTimer = 5.f;
	Mathf::Vector3 directionToTarget = { 0.f, 0.f, 0.f };
};
