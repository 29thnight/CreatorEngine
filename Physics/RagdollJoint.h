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
	inline const DirectX::SimpleMath::Matrix& GetLocalTransform() const { return m_localTransform; }
	inline const DirectX::SimpleMath::Matrix& GetSimulLocalTransform() const { return m_simulLocalTransform; }
	inline const DirectX::SimpleMath::Matrix& GetSimulOffsetTransform() const { return m_simulOffsetTransform; }
	inline const DirectX::SimpleMath::Matrix& GetSimulWorldTransform() const { return m_simulWorldTransform; }
	inline const physx::PxArticulationJointReducedCoordinate* GetPxJoint() const { return m_pxJoint; }
	inline const physx::PxArticulationDrive& GetDrive() const { return m_drive; }
	inline const physx::PxArticulationLimit& GetXLimit() const { return m_xLimit; }
	inline const physx::PxArticulationLimit& GetYLimit() const { return m_yLimit; }
	inline const physx::PxArticulationLimit& GetZLimit() const { return m_zLimit; }

private:
	RagdollLink* m_OwnerLink; //¼ÒÀ¯ ¸µÅ©
	RagdollLink* m_parentLink; //ºÎ¸ð ¸µÅ©

	DirectX::SimpleMath::Matrix m_localTransform; //·ÎÄÃ Æ®·»½ºÆû
	DirectX::SimpleMath::Matrix m_simulOffsetTransform; //½Ã¹Ä·¹ÀÌ¼Ç ¿ÀÇÁ¼Â Æ®·»½ºÆû
	DirectX::SimpleMath::Matrix m_simulLocalTransform; //½Ã¹Ä·¹ÀÌ¼Ç ·ÎÄÃ Æ®·»½ºÆû
	DirectX::SimpleMath::Matrix m_simulWorldTransform; //½Ã¹Ä·¹ÀÌ¼Ç ¿ùµå Æ®·»½ºÆû

	physx::PxArticulationJointReducedCoordinate* m_pxJoint; //°üÀý

	physx::PxArticulationDrive m_drive;
	physx::PxArticulationLimit m_xLimit;
	physx::PxArticulationLimit m_yLimit;
	physx::PxArticulationLimit m_zLimit;
};

