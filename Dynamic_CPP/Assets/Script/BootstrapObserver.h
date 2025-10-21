#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class BootstrapObserver : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BootstrapObserver)
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
	class SoundComponent* m_pSoundObject{ nullptr };
	class GameObject* m_pBootstrapObject{ nullptr };
};
