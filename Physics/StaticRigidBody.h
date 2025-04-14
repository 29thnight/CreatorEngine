#pragma once
#include "PhysicsHelper.h"
#include "RigidBody.h"

class StaticRigidBody : public RigidBody
{
public:
	StaticRigidBody(EColliderType collidreType, unsigned int id, unsigned int layerNumber);
	virtual ~StaticRigidBody();

	//»ý¼º
	bool Initialize(ColliderInfo colliderInfo, physx::PxShape* shape, physx::PxPhysics* physics, CollisionData* data);

	void ChangeLayerNumber(const unsigned int& layerNumber, int* collisionMatrix);

	void SetConvertScale(const Mathf::Vector3& scale, physx::PxPhysics* physics, int* collisionMatrix) override;

	physx::PxRigidStatic* GetRigidStatic() const { return m_rigidStatic; } // 

private:
	physx::PxRigidStatic* m_rigidStatic; //
};
