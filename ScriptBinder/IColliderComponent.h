#pragma once
#include "../physics/PhysicsCommon.h"


class IColliderComponent
{
public:
	virtual ~IColliderComponent() = default;

	// PhysicsEventCallback ���� ����ġ�� �� ȣ��� �Լ���
	virtual void OnCollisionEnter(const CollisionData& data) {}
	virtual void OnCollisionStay(const CollisionData& data) {}
	virtual void OnCollisionExit(const CollisionData& data) {}

	virtual void OnTriggerEnter(const CollisionData& data) {}
	virtual void OnTriggerStay(const CollisionData& data) {}
	virtual void OnTriggerExit(const CollisionData& data) {}
};
