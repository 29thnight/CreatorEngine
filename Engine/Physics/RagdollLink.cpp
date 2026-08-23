#include "RagdollLink.h"

bool RagdollLink::Initialize(const LinkInfo& linkInfo, RagdollLink* parentLink, physx::PxArticulationReducedCoordinate* pxArtriculation)
{
	m_name = linkInfo.boneName;
	m_density = linkInfo.density;
	m_localTransform = linkInfo.localTransform;
	m_parentLink = parentLink;

	physx::PxTransform pxLocalTransform;
	DirectX::SimpleMath::Vector3 scale;
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	m_localTransform.Decompose(scale, rotation, position);

	//왜 스케일 적용을 안하는지? --> // 물리엔진에서 스케일을 적용하지 않기 때문
	DirectX::SimpleMath::Matrix dxTransform = DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) *
		DirectX::SimpleMath::Matrix::CreateTranslation(position);

	if (parentLink == nullptr) {
		//부모가 없는 경우
		dxTransform *= DirectX::SimpleMath::Matrix::CreateRotationZ(3.14f);
		dxTransform.Decompose(scale, rotation, position);

		ConvertVectorDxToPx(position, pxLocalTransform.p);
		ConvertQuaternionDxToPx(rotation, pxLocalTransform.q);
		//CopyMatrixDxToPx(dxTransform, pxLocalTransform);

		m_pxLink = pxArtriculation->createLink(nullptr, pxLocalTransform);
	}
	else {
		//부모가 있는 경우
		dxTransform.Decompose(scale, rotation, position);
		ConvertVectorDxToPx(position, pxLocalTransform.p);
		ConvertQuaternionDxToPx(rotation, pxLocalTransform.q);
		//CopyMatrixDxToPx(dxTransform, pxLocalTransform);


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
	physx::PxTransform pxTransform;
	pxTransform = m_pxLink->getGlobalPose();

	DirectX::SimpleMath::Vector3 scale;
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	m_worldTransform.Decompose(scale, rotation, position);

	ConvertVectorDxToPx(position, pxTransform.p);
	ConvertQuaternionDxToPx(rotation, pxTransform.q);
	
	//CopyMatrixDxToPx(m_worldTransform, pxTransform);

	return m_myJoint->Update(m_parentLink->GetPxLink());
}

physx::PxShape* RagdollLink::CreateShape(physx::PxMaterial* material, const DirectX::SimpleMath::Vector3& extent, CollisionData* collisionData)
{
	physx::PxVec3 pxExtent;
	std::memcpy(&pxExtent, &extent, sizeof(DirectX::SimpleMath::Vector3));

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



void RagdollLink::SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform)
{
	DirectX::SimpleMath::Matrix obejctTransform = worldTransform;

	DirectX::SimpleMath::Vector3 scale;
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	obejctTransform.Decompose(scale, rotation, position);

	DirectX::SimpleMath::Matrix dxTransform = DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) *
		DirectX::SimpleMath::Matrix::CreateTranslation(position) *
		m_myJoint->GetLocalTransform().Invert();

	physx::PxTransform pxTransform;
	dxTransform.Decompose(scale, rotation, position);
	ConvertVectorDxToPx(position, pxTransform.p);
	ConvertQuaternionDxToPx(rotation, pxTransform.q);
	//CopyMatrixDxToPx(dxTransform, pxTransform);

	//physx::PxTransform prevTransform = m_pxLink->getGlobalPose(); //임시 변수로 prev 받아서 어따씀?

	m_pxLink->setGlobalPose(pxTransform);
}
