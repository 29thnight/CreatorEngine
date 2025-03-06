#pragma once
#include "MonoBehavior.h"
class Object;
class InteractableComponent : public MonoBehavior
{
public:
	virtual bool IsInteractable() = 0;
public:
	virtual void Interact() = 0;
	virtual void Interact(Object* obj) = 0;

	// Component을(를) 통해 상속됨
	virtual void Initialize() override = 0;
	virtual void FixedUpdate(float fixedTick) override = 0;
	virtual void Update(float tick) override = 0;
	virtual void LateUpdate(float tick) override = 0;
	virtual void EditorContext() override = 0;
	virtual void Serialize(_inout json& out) override = 0;
	virtual void DeSerialize(_in json& in) override = 0;

	// MonoBehavior을(를) 통해 상속됨
	virtual void OnTriggerEnter(ICollider* other) override = 0;
	virtual void OnTriggerStay(ICollider* other) override = 0;
	virtual void OnTriggerExit(ICollider* other) override = 0;
	virtual void OnCollisionEnter(ICollider* other) override = 0;
	virtual void OnCollisionStay(ICollider* other) override = 0;
	virtual void OnCollisionExit(ICollider* other) override = 0;

	uint32 ID() override { return 0; }
};

