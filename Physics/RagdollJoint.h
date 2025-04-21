#pragma once
#include <physx/PxPhysicsAPI.h>
#include "RagdollLink.h"

constexpr float CircularMeasure = 0.01744f;

class RagdollLink;
class RagdollJoint
{
public:
	RagdollJoint();
	~RagdollJoint();

	bool Initialize(RagdollLink* paranthLink,RagdollLink* ownerLink, const JointInfo& info);
	bool Update(const physx::PxArticulationLink* paranthLink);

	inline const RagdollLink* GetOwnerLink() const { return m_OwnerLink; }
	inline const RagdollLink* GetParentLink() const { return m_parentLink; }
	inline const Mathf::Matrix& GetLocalTransform() const { return m_localTransform; }
	inline const Mathf::Matrix& GetSimulLocalTransform() const { return m_simulLocalTransform; }
	inline const Mathf::Matrix& GetSimulOffsetTransform() const { return m_simulOffsetTransform; }
	inline const Mathf::Matrix& GetSimulWorldTransform() const { return m_simulWorldTransform; }
	inline const physx::PxArticulationJointReducedCoordinate* GetPxJoint() const { return m_pxJoint; }
	inline const physx::PxArticulationDrive& GetDrive() const { return m_drive; }
	inline const physx::PxArticulationLimit& GetXLimit() const { return m_xLimit; }
	inline const physx::PxArticulationLimit& GetYLimit() const { return m_yLimit; }
	inline const physx::PxArticulationLimit& GetZLimit() const { return m_zLimit; }

private:
	RagdollLink* m_OwnerLink; //���� ��ũ
	RagdollLink* m_parentLink; //�θ� ��ũ

	Mathf::Matrix m_localTransform; //���� Ʈ������
	Mathf::Matrix m_simulOffsetTransform; //�ùķ��̼� ������ Ʈ������
	Mathf::Matrix m_simulLocalTransform; //�ùķ��̼� ���� Ʈ������
	Mathf::Matrix m_simulWorldTransform; //�ùķ��̼� ���� Ʈ������

	physx::PxArticulationJointReducedCoordinate* m_pxJoint; //����

	physx::PxArticulationDrive m_drive;
	physx::PxArticulationLimit m_xLimit;
	physx::PxArticulationLimit m_yLimit;
	physx::PxArticulationLimit m_zLimit;
};

