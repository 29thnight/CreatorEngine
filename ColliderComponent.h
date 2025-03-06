#pragma once
#include "Component.h"
#include "Physics/ICollider.h"
#include "Physics/PhysicsInfo.h"
#include <physx/PxPhysicsAPI.h>
#include "ObjectTypeMeta.h"
class RigidbodyComponent;
class ColliderComponent : public Component, public ICollider
{
public:
	ColliderComponent();

	virtual ~ColliderComponent();
	// Component을(를) 통해 상속됨
	void Initialize() override = 0;
	void FixedUpdate(float fixedTick) override = 0;
	void Update(float tick) override = 0;
	void LateUpdate(float tick) override = 0;;
	void EditorContext() override = 0;

	// ICollider을(를) 통해 상속됨
	void SetLocalPosition(std::tuple<float, float, float> pos) override;
	void SetRotation(std::tuple<float, float, float, float> rotation) override;
	void SetIsTrigger(bool isTrigger) override;
	bool GetIsTrigger() override;
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;

	void SetEnable(bool enable);
	void SetFilter();

	Object* GetOwner() override;

	uint32 ID() override { return _ID; }

protected:
	physx::PxShape* mShape = nullptr;
	//physx::PxRigidActor* mRigidbody = nullptr;
	RigidbodyComponent* rigidbody = nullptr;

	ColliderInfo info;

	friend class RigidbodyComponent;

	virtual void Serialize(_inout json& out) override = 0;
	virtual void DeSerialize(_in json& in) override = 0;

private:
	static constexpr uint32 _ID{ 4002 };
};

