#pragma once
#include "Component.h"
#include "Physics/IRigidbody.h"
#include "Physics/PhysicsInfo.h"
#include <physx/PxPhysicsAPI.h>
#include "ObjectTypeMeta.h"

class ColliderComponent;
class RigidbodyComponent : public Component, public virtual IRigidbody
{
public:
	RigidbodyComponent();

	virtual ~RigidbodyComponent();
	// Component을(를) 통해 상속됨
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;
	virtual void Serialize(_inout json& out) override final;
	virtual void DeSerialize(_in json& in) override final;

	void SetCollider(ColliderComponent* collider);

	// IRigidbody을(를) 통해 상속됨
	std::tuple<float, float, float> GetWorldPosition() override;
	std::tuple<float, float, float, float> GetWorldRotation() override;
	void SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot) override;
	void AddForce(float* velocity, ForceMode mode) override;
	RigidbodyInfo* GetInfo() override;

public:
	RigidbodyInfo info;

	physx::PxRigidActor* rbActor;


	// IRigidbody을(를) 통해 상속됨
	void AddTorque(float* velocity, ForceMode mode) override;

	uint32 ID() override { return _ID; }

private:
	void CreateActor(ColliderComponent* collider);

	// Component을(를) 통해 상속됨
	static constexpr uint32 _ID{ 4001 };
};

template<>
struct MetaType<RigidbodyComponent>
{
	static constexpr std::string_view type{ "RigidbodyComponent" };
};