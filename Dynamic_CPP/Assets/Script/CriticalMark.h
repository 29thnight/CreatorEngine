#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "CriticalMark.generated.h"

class EffectComponent;
class CriticalMark : public ModuleBehavior
{
public:
   ReflectCriticalMark
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(CriticalMark)
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

	void ResetMark();
	bool UpdateMark(int _playerindex);
private:
	EffectComponent* markEffect = nullptr;
	bool OnMark = false;
	bool canMark = true;  //마크 생성가능
	[[Property]]
	float markDuration = 5.0f; //마크지속시간
	float markElaspedTime = 0.f;
	[[Property]]
	float markCoolDown = 5.0f;
	float markCoolElaspedTime = 0.f;;
	int markIndex = -1;
};
