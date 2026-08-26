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
	inline const math::matrix4x4& GetLocalTransform() const { return m_localTransform; }
	inline const math::matrix4x4& GetSimulLocalTransform() const { return m_simulLocalTransform; }
	inline const math::matrix4x4& GetSimulOffsetTransform() const { return m_simulOffsetTransform; }
	inline const math::matrix4x4& GetSimulWorldTransform() const { return m_simulWorldTransform; }
	inline const physx::PxArticulationJointReducedCoordinate* GetPxJoint() const { return m_pxJoint; }
	inline const physx::PxArticulationDrive& GetDrive() const { return m_drive; }
	inline const physx::PxArticulationLimit& GetXLimit() const { return m_xLimit; }
	inline const physx::PxArticulationLimit& GetYLimit() const { return m_yLimit; }
	inline const physx::PxArticulationLimit& GetZLimit() const { return m_zLimit; }

private:
	RagdollLink* m_OwnerLink; //¼ÒÀ¯ ¸µÅ©
	RagdollLink* m_parentLink; //ºÎ¸ð ¸µÅ©

	math::matrix4x4 m_localTransform = math::matrix4x4::identity(); //·ÎÄÃ Æ®·»½ºÆû
	math::matrix4x4 m_simulOffsetTransform = math::matrix4x4::identity(); //½Ã¹Ä·¹ÀÌ¼Ç ¿ÀÇÁ¼Â Æ®·»½ºÆû
	math::matrix4x4 m_simulLocalTransform = math::matrix4x4::identity(); //½Ã¹Ä·¹ÀÌ¼Ç ·ÎÄÃ Æ®·»½ºÆû
	math::matrix4x4 m_simulWorldTransform = math::matrix4x4::identity(); //½Ã¹Ä·¹ÀÌ¼Ç ¿ùµå Æ®·»½ºÆû

	physx::PxArticulationJointReducedCoordinate* m_pxJoint; //°üÀý

	physx::PxArticulationDrive m_drive;
	physx::PxArticulationLimit m_xLimit;
	physx::PxArticulationLimit m_yLimit;
	physx::PxArticulationLimit m_zLimit;
};

