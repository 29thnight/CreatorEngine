#pragma once
#include "PhysicsHelper.h"
#include "RigidBody.h"
#include "../Utility_framework/LogSystem.h"
class DynamicRigidBody : public RigidBody
{
public:
	DynamicRigidBody(EColliderType collidreType, unsigned int id, unsigned int layerNumber);
	virtual ~DynamicRigidBody();
	bool Initialize(ColliderInfo colliderInfo, physx::PxShape* shape, physx::PxPhysics* physics, CollisionData* data, bool isKinematic);
	void ChangeLayerNumber(const unsigned int& layerNumber, int* collisionMatrix);
	void SetConvertScale(const DirectX::SimpleMath::Vector3& scale, physx::PxPhysics* physics, int* collisionMatrix) override;
	physx::PxRigidDynamic* GetRigidDynamic() const { return m_rigidDynamic; } // 

private:
	physx::PxRigidDynamic* m_rigidDynamic; //
};
