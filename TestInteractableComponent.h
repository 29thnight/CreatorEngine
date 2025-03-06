#pragma once
#include "InteractableComponent.h"
#include "Physics/Common.h"
class TestInteractableComponent : public InteractableComponent
{
public:
	// InteractableComponent을(를) 통해 상속됨
	bool IsInteractable() override;
	void Interact() override;
	void Interact(Object* obj) override;
public:
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;
	virtual void Serialize(_inout json& out) override {};
	virtual void DeSerialize(_in json& in) override {};

	// InteractableComponent을(를) 통해 상속됨
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;

	uint32 ID() override { return 0; }
	// InteractableComponent을(를) 통해 상속됨
	void BurnOut() override;

};

