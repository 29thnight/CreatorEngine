#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include <functional>

using namespace Mathf;
class TweenManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(TweenManager)
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
	virtual void OnDestroy() override;

public:
	void AddTween(std::shared_ptr<ITween> tween);
	void UpdateTween(float deltaTime);
	void KillAll();

private:
	std::vector<std::shared_ptr<ITween>> activeTweens;
	std::vector<std::shared_ptr<ITween>> tweensToAdd;
};
