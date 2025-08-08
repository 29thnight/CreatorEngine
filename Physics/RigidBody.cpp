#include "RigidBody.h"

RigidBody::RigidBody(EColliderType collidreType, unsigned int id, unsigned int layerNumber)
	: m_colliderType(collidreType)
	, m_id(id)
	, m_layerNumber(layerNumber)
	, m_radius()
	, m_halfHeight()
	, m_Extent()
	, m_scale(DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f))
	, m_offsetRotation(DirectX::SimpleMath::Quaternion::Identity)
	, m_offsetPsition(DirectX::SimpleMath::Vector3::Zero)
{

}

RigidBody::~RigidBody()
{
}

void RigidBody::UpdateShapeGeometry(physx::PxRigidActor* Actor, const physx::PxGeometry& newGeometry, physx::PxPhysics* physics, physx::PxMaterial* material, unsigned int* collisionMatrix, void* userData)
{
	physx::PxShape* newShape = physics->createShape(newGeometry, *material, true);
	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];
	newShape->setSimulationFilterData(filterData);
	newShape->setContactOffset(0.02f);
	newShape->setRestOffset(0.01f);
	newShape->userData = userData;
	userData = nullptr;

	if (m_colliderType == EColliderType::COLLISION)
	{
		newShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
	}
	else {
		newShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
		newShape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	physx::PxShape* oldShape = nullptr;
	Actor->getShapes(&oldShape, 1);
	if (oldShape != nullptr)
	{
		Actor->detachShape(*oldShape);
	}
	Actor->attachShape(*newShape);

	newShape->release();
}