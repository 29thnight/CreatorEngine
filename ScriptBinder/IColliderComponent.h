#pragma once
#include "../physics/PhysicsCommon.h"


class IColliderComponent
{
public:
	virtual ~IColliderComponent() = default;

	// PhysicsEventCallback 에서 디스패치될 때 호출될 함수들
	virtual void OnCollisionEnter(const CollisionData& data) {}
	virtual void OnCollisionStay(const CollisionData& data) {}
	virtual void OnCollisionExit(const CollisionData& data) {}

	virtual void OnTriggerEnter(const CollisionData& data) {}
	virtual void OnTriggerStay(const CollisionData& data) {}
	virtual void OnTriggerExit(const CollisionData& data) {}
};
