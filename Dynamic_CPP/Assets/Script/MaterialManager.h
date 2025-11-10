#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "MaterialManager.generated.h"

class MaterialManager : public ModuleBehavior
{
public:
   ReflectMaterialManager
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(MaterialManager)
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

	// 메쉬별로 지정하는 색이 다름, 
	[[Method]]
	void SetGradationMaterials();

	[[Property]]
	float leftPosition = 0.f;
	[[Property]]
	float rightPosition = 0.f;

	[[Property]]
	Mathf::Vector3 leftColor{};
	[[Property]]
	Mathf::Vector3 rightColor{};
};
