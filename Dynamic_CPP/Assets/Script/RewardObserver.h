#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class RewardObserver : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(RewardObserver)
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

private:
	class TextComponent* rewardText = nullptr;
	class ImageComponent* rewardImage = nullptr;
	class GameManager* gameManager = nullptr;

private:
	int   m_prevReward{};
	float m_prevRatio{};
	bool  m_activeEffect{ false };
	float m_elapsed{};
	float m_setEffectTime{ 1.5f };
};
