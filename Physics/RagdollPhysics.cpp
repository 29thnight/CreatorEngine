#include "RagdollPhysics.h"

RagdollPhysics::RagdollPhysics()
{
}

RagdollPhysics::~RagdollPhysics()
{
}

void RagdollPhysics::Initialize(const ArticulationInfo& info, physx::PxPhysics* physics, CollisionData* collisionData)
{
	m_collisionData = collisionData;
	m_pxArticulation = physics->createArticulationReducedCoordinate(); //���� ����
	m_pxArticulation->setArticulationFlag(physx::PxArticulationFlag::eFIX_BASE, false);  //���̽� ������ �ƴ� 
	m_pxArticulation->setArticulationFlag(physx::PxArticulationFlag::eDISABLE_SELF_COLLISION, false); //�ڱ� �浹 ��Ȱ��ȭ
	m_pxArticulation->setSolverIterationCounts(32); //Ragdoll ���� solver �ݺ� ��� Ƚ��
	m_pxArticulation->setMaxCOMLinearVelocity(10.0f); //COM ���� �ӵ� ����
	m_pxArticulation->setMaxCOMAngularVelocity(10.0f); //COM ���ӵ� ����

	m_material = physics->createMaterial(info.staticFriction, info.dynamicFriction, info.restitution); //���� ���� ����
	m_id = info.id;
	m_layerNumber = info.layerNumber;
	m_worldTransform = info.worldTransform;

}

void RagdollPhysics::Update(float deltaTime)
{
	for (auto& [name, link] : m_linkContainer)
	{
		if (m_bIsRagdoll) {
			/*
			if (!link->Update())
			{
				return false;
			}
			*/
		}
	}
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix, const Mathf::Vector3& extend)
{
	RagdollLink* link = new RagdollLink();

	auto parentLink = m_linkContainer.find(linkInfo.parentBoneName);

	if (parentLink == m_linkContainer.end())
	{
		if (!link->)
		{

		}
	}

	
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix, const float& radius)
{
	return false;
}

bool RagdollPhysics::AddArticulationLink(const LinkInfo& linkInfo, int* collisionMatrix, const float& halfHeight, const float& radius)
{
	return false;
}

bool RagdollPhysics::AddArticulationLink(LinkInfo& linkInfo, int* collisionMatrix)
{
	return false;
}

bool RagdollPhysics::ChangeLayerNumber(const unsigned int& newLayerNumber, int* collisionMatrix)
{
	return false;
}

void RagdollPhysics::SetWorldTransform(const Mathf::Matrix& worldTransform)
{
}

bool RagdollPhysics::SetLinkTransformUpdate(const std::string& name, const Mathf::Matrix& boneWorldTransform)
{
	return false;
}
