#include "RagdollLink.h"
#include "PhysicsMathAdapter.h"
#include <mathematics/transform.hpp>

bool RagdollLink::Initialize(const LinkInfo& linkInfo, RagdollLink* parentLink, physx::PxArticulationReducedCoordinate* pxArtriculation)
{
	m_name = linkInfo.boneName;
	m_density = linkInfo.density;
	m_localTransform = linkInfo.localTransform;
	m_parentLink = parentLink;

	math::vector3 scale{};
	math::quaternion rotation{};
	math::vector3 position{};
	(void)math::decompose(m_localTransform, scale, rotation, position);

	math::matrix4x4 linkTransform = math::compose(
		math::vector3::one(), rotation, position);

	if (parentLink == nullptr)
	{
		linkTransform *= math::rotation_z(3.14f);
		(void)math::decompose(linkTransform, scale, rotation, position);
		const physx::PxTransform pxLocalTransform =
			PhysicsMath::ToPxTransform(position, rotation);
		m_pxLink = pxArtriculation->createLink(nullptr, pxLocalTransform);
	}
	else
	{
		(void)math::decompose(linkTransform, scale, rotation, position);
		const physx::PxTransform pxLocalTransform =
			PhysicsMath::ToPxTransform(position, rotation);
		m_pxLink = pxArtriculation->createLink(parentLink->GetPxLink(), pxLocalTransform);
		m_myJoint->Initialize(parentLink, this, linkInfo.jointInfo);
	}

	m_pxLink->setMaxAngularVelocity(4.0f);
	m_pxLink->setMaxLinearVelocity(4.0f);
	m_pxLink->setAngularDamping(0.2f);
	m_pxLink->setLinearDamping(0.2f);

	return true;
}
bool RagdollLink::Update()
{
	return m_myJoint->Update(m_parentLink->GetPxLink());
}
physx::PxShape* RagdollLink::CreateShape(physx::PxMaterial* material, const math::vector3& extent, CollisionData* collisionData)
{
	const physx::PxVec3 pxExtent = PhysicsMath::ToPx(extent);

	physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*m_pxLink, physx::PxBoxGeometry(pxExtent), *material);
	physx::PxRigidBodyExt::updateMassAndInertia(*m_pxLink, m_density);

	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + m_name + "] create Shape fail");
		return nullptr;
	}

	shape->userData = collisionData;
	shape->setContactOffset(0.002f);
	shape->setRestOffset(0.001f);

	return shape;
}
physx::PxShape* RagdollLink::CreateShape(physx::PxMaterial* material, const float& radius, const float& halfHeight, CollisionData* collisionData)
{
	physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*m_pxLink, physx::PxCapsuleGeometry(radius, halfHeight), *material);
	physx::PxRigidBodyExt::updateMassAndInertia(*m_pxLink, m_density);
	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + m_name + "] create Shape fail");
		return nullptr;
	}

	shape->userData = collisionData;
	shape->setContactOffset(0.002f);
	shape->setRestOffset(0.001f);

	return shape;
}

physx::PxShape* RagdollLink::CreateShape(physx::PxMaterial* material, const float& radius, CollisionData* collisionData)
{
	physx::PxShape* shape = physx::PxRigidActorExt::createExclusiveShape(*m_pxLink, physx::PxSphereGeometry(radius), *material);
	physx::PxRigidBodyExt::updateMassAndInertia(*m_pxLink, m_density);
	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + m_name + "] create Shape fail");
		return nullptr;
	}

	shape->userData = collisionData;
	shape->setContactOffset(0.002f);
	shape->setRestOffset(0.001f);

	return shape;
}

bool RagdollLink::ChangeLayerNumber(const physx::PxFilterData& fillterData, CollisionData* collisionData)
{
	physx::PxShape* shape;
	m_pxLink->getShapes(&shape, 1);

	shape->setSimulationFilterData(fillterData);
	shape->userData = collisionData;

	return true;
}



void RagdollLink::SetWorldTransform(const math::matrix4x4& worldTransform)
{
	math::vector3 scale{};
	math::quaternion rotation{};
	math::vector3 position{};
	(void)math::decompose(worldTransform, scale, rotation, position);

	const math::matrix4x4 linkTransform =
		math::compose(math::vector3::one(), rotation, position) *
		math::inverse(m_myJoint->GetLocalTransform());
	(void)math::decompose(linkTransform, scale, rotation, position);

	m_pxLink->setGlobalPose(PhysicsMath::ToPxTransform(position, rotation));
}
