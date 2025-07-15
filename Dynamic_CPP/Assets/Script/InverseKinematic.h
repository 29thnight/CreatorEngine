#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
using namespace Mathf;
class InverseKinematic : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(InverseKinematic)
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
	virtual void LateUpdate(float tick) override;
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

private:
	GameObject* target{ nullptr };
	GameObject* pole{ nullptr };

	GameObject* firstBone;
	Vector3 firstBoneEulerAngleOffset{ 0, 90, 0 };
	GameObject* secondBone;
	Vector3 secondBoneEularAngleOffset{ 0, 90, 0 };
	GameObject* thirdBone;
	Vector3 thirdBoneEularAngleOffset{ 0, 90, 0 };
	bool alignThirdBoneWithTargetRotation = true;
};
