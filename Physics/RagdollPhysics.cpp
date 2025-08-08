#include "RagdollPhysics.h"

RagdollPhysics::RagdollPhysics()
{
}

RagdollPhysics::~RagdollPhysics()
{
	m_collisionData->isDead = true;
	if (m_material)
	{
		m_material->release();
		m_material = nullptr;
	}
}

void RagdollPhysics::Initialize(const ArticulationInfo& info, physx::PxPhysics* physics, CollisionData* collisionData)
{
	m_collisionData = collisionData;
	m_pxArticulation = physics->createArticulationReducedCoordinate(); //관절 생성
	m_pxArticulation->setArticulationFlag(physx::PxArticulationFlag::eFIX_BASE, false);  //베이스 고정이 아님 
	m_pxArticulation->setArticulationFlag(physx::PxArticulationFlag::eDISABLE_SELF_COLLISION, false); //자기 충돌 비활성화
	m_pxArticulation->setSolverIterationCounts(32); //Ragdoll 하위 solver 반복 계산 횟수
	m_pxArticulation->setMaxCOMLinearVelocity(10.0f); //COM 선형 속도 제한
	m_pxArticulation->setMaxCOMAngularVelocity(10.0f); //COM 각속도 제한

	m_material = physics->createMaterial(info.staticFriction, info.dynamicFriction, info.restitution); //물리 재질 생성
	m_id = info.id;
	m_layerNumber = info.layerNumber;
	m_worldTransform = info.worldTransform;

}

void RagdollPhysics::Update(float deltaTime)
{
	for (auto& [name, link] : m_linkContainer)
	{
		if (m_bIsRagdoll) {
			link->Update();
		}
	}
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, unsigned int* collisionMatrix, const DirectX::SimpleMath::Vector3& extend)
{
	RagdollLink* link = new RagdollLink();

	auto parentLink = m_linkContainer.find(linkInfo.parentBoneName);

	if (parentLink == m_linkContainer.end())
	{
		if (!link->Initialize(linkInfo,m_rootLink,m_pxArticulation))
		{
			return false;
		}
	}
	else
	{
		if (!link->Initialize(linkInfo, parentLink->second, m_pxArticulation))
		{
			return false;
		}
	}

	physx::PxShape* shape = link->CreateShape(m_material, extend, m_collisionData);
	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + linkInfo.boneName + "] create Shape fail");
		return false;
	}

	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];
	filterData.word2 = 1;
	shape->setSimulationFilterData(filterData);

	m_linkContainer.insert(std::make_pair(linkInfo.boneName, link));

	return true;
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, unsigned int* collisionMatrix, const float& radius)
{
	RagdollLink* link = new RagdollLink();
	auto parentLink = m_linkContainer.find(linkInfo.parentBoneName);
	if (parentLink == m_linkContainer.end())
	{
		if (!link->Initialize(linkInfo, m_rootLink, m_pxArticulation))
		{
			return false;
		}
	}
	else
	{
		if (!link->Initialize(linkInfo, parentLink->second, m_pxArticulation))
		{
			return false;
		}
	}
	physx::PxShape* shape = link->CreateShape(m_material, radius, m_collisionData);
	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + linkInfo.boneName + "] create Shape fail");
		return false;
	}
	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];
	filterData.word2 = 1;
	shape->setSimulationFilterData(filterData);
	m_linkContainer.insert(std::make_pair(linkInfo.boneName, link));
	return true;
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, unsigned int* collisionMatrix, const float& halfHeight, const float& radius)
{
	RagdollLink* link = new RagdollLink();
	auto parentLink = m_linkContainer.find(linkInfo.parentBoneName);
	if (parentLink == m_linkContainer.end())
	{
		if (!link->Initialize(linkInfo, m_rootLink, m_pxArticulation))
		{
			return false;
		}
	}
	else
	{
		if (!link->Initialize(linkInfo, parentLink->second, m_pxArticulation))
		{
			return false;
		}
	}
	physx::PxShape* shape = link->CreateShape(m_material, radius, halfHeight, m_collisionData);
	if (shape == nullptr)
	{
		Debug->LogError("ragdoll link error [ nema :" + linkInfo.boneName + "] create Shape fail");
		return false;
	}
	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];
	filterData.word2 = 1;
	shape->setSimulationFilterData(filterData);
	m_linkContainer.insert(std::make_pair(linkInfo.boneName, link));
	return true;
}

bool RagdollPhysics::AddArticulationLink(LinkInfo& linkInfo, unsigned int* collisionMatrix)
{
	m_rootLink = new RagdollLink();

	linkInfo.localTransform = m_worldTransform * linkInfo.localTransform;
	m_rootLink->Initialize(linkInfo, nullptr, m_pxArticulation);

	return true;
}

bool RagdollPhysics::ChangeLayerNumber(const unsigned int& newLayerNumber, unsigned int* collisionMatrix)
{
	if (newLayerNumber == UINT_MAX) {
		return false;
	}

	physx::PxFilterData newFilterData;
	newFilterData.word0 = newLayerNumber;
	newFilterData.word1 = collisionMatrix[newLayerNumber];
	newFilterData.word2 = 1;

	m_collisionData->thisId = m_id;
	m_collisionData->thisLayerNumber = newLayerNumber;

	for (auto& [name, link] : m_linkContainer)
	{
		link->ChangeLayerNumber(newFilterData, m_collisionData);
	}

}

void RagdollPhysics::SetWorldTransform(const DirectX::SimpleMath::Matrix& worldTransform)
{
	m_worldTransform = worldTransform;
	DirectX::SimpleMath::Vector3 scale = { 1.0f,1.0f,1.0f };
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	m_worldTransform.Decompose(scale, rotation, position);


	physx::PxTransform pxTransform;
	ConvertVectorDxToPx(position, pxTransform.p);
	ConvertQuaternionDxToPx(rotation, pxTransform.q);
	//CopyMatrixDxToPx(dxTransform, pxTransform);
	m_pxArticulation->setRootGlobalPose(pxTransform);
}

bool RagdollPhysics::SetLinkTransformUpdate(const std::string& name, const DirectX::SimpleMath::Matrix& boneWorldTransform)
{
	auto link = m_linkContainer.find(name);
	
	link->second->SetWorldTransform(boneWorldTransform);

	m_worldTransform = boneWorldTransform;
	DirectX::SimpleMath::Vector3 scale = { 1.0f,1.0f,1.0f };
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	m_worldTransform.Decompose(scale, rotation, position);

	
	physx::PxTransform pxTransform;
	ConvertVectorDxToPx(position, pxTransform.p);
	ConvertQuaternionDxToPx(rotation, pxTransform.q);

	//CopyMatrixDxToPx(dxTransform, pxTransform);

	m_pxArticulation->setRootGlobalPose(pxTransform);

	return true;
}
