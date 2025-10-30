#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "BGMChangeTrigger.generated.h"
class BGMChangeTrigger : public ModuleBehavior
{
public:
   ReflectBGMChangeTrigger
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(BGMChangeTrigger)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
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
	[[Property]]
	std::string nextBGMTag = "None";
	[[Property]]
	std::string nextAmbienceTag = "None";

	GameObject* BGM = nullptr;
	GameObject* BGMAmbience = nullptr;
	bool isSwapped = false; //이미바꼇는지
};
