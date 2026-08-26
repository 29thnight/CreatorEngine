#include "RagdollJoint.h"
#include "PhysicsMathAdapter.h"
#include <mathematics/transform.hpp>

bool RagdollJoint::Initialize(RagdollLink* paranthLink,RagdollLink* ownerLink, const JointInfo& info)
{
	m_parentLink = paranthLink;
	m_OwnerLink = ownerLink;
	m_localTransform = info.localTransform;
	m_xLimit.high = info.xAxisInfo.limitHigh;
	m_xLimit.low = info.xAxisInfo.limitlow;
	m_yLimit.high = info.yAxisInfo.limitHigh;
	m_yLimit.low = info.yAxisInfo.limitlow;
	m_zLimit.high = info.zAxisInfo.limitHigh;
	m_zLimit.low = info.zAxisInfo.limitlow;
	m_drive.damping = info.damping;
	m_drive.maxForce = info.maxForce;
	m_drive.stiffness = info.stiffness;
	m_drive.driveType = physx::PxArticulationDriveType::eFORCE;

	m_pxJoint = ownerLink->GetPxLink()->getInboundJoint();
	m_pxJoint->setMaxJointVelocity(5.0f);
	m_pxJoint->setFrictionCoefficient(0.1f);

	math::vector3 scale{};
	math::quaternion rotation{};
	math::vector3 position{};
	(void)math::decompose(m_localTransform, scale, rotation, position);
	m_localTransform = math::compose(scale, rotation, position);

	const physx::PxTransform pxLocalTransform =
		PhysicsMath::ToPxTransform(position, rotation);
	m_pxJoint->setChildPose(pxLocalTransform);
	m_pxJoint->setParentPose(pxLocalTransform);

	if (info.xAxisInfo.motion == EArticulationMotion::LOCKED && info.yAxisInfo.motion==EArticulationMotion::LOCKED&&info.zAxisInfo.motion==EArticulationMotion::LOCKED)
	{
		m_pxJoint->setJointType(physx::PxArticulationJointType::eFIX);
	}
	else {
		m_pxJoint->setJointType(physx::PxArticulationJointType::eSPHERICAL);
	}

	m_pxJoint->setMotion(physx::PxArticulationAxis::eSWING1, (physx::PxArticulationMotion::Enum)info.xAxisInfo.motion);
	m_pxJoint->setMotion(physx::PxArticulationAxis::eSWING2, (physx::PxArticulationMotion::Enum)info.yAxisInfo.motion);
	m_pxJoint->setMotion(physx::PxArticulationAxis::eTWIST, (physx::PxArticulationMotion::Enum)info.zAxisInfo.motion);

	physx::PxArticulationLimit pxXlimit(m_xLimit.low * CircularMeasure, m_xLimit.high * CircularMeasure);
	physx::PxArticulationLimit pxYlimit(m_yLimit.low * CircularMeasure, m_yLimit.high * CircularMeasure);
	physx::PxArticulationLimit pxZlimit(m_zLimit.low * CircularMeasure, m_zLimit.high * CircularMeasure);

	m_pxJoint->setLimitParams(physx::PxArticulationAxis::eSWING1, pxXlimit);
	m_pxJoint->setLimitParams(physx::PxArticulationAxis::eSWING2, pxYlimit);
	m_pxJoint->setLimitParams(physx::PxArticulationAxis::eTWIST, pxZlimit);

	m_pxJoint->setDriveParams(physx::PxArticulationAxis::eSWING1, m_drive);
	m_pxJoint->setDriveParams(physx::PxArticulationAxis::eSWING2, m_drive);
	m_pxJoint->setDriveParams(physx::PxArticulationAxis::eTWIST, m_drive);

	return true;
}
bool RagdollJoint::Update(const physx::PxArticulationLink* paranthLink)
{
	if (!m_pxJoint)
	{
		return true;
	}

	physx::PxArticulationLink& childLink = m_pxJoint->getChildArticulationLink();

	physx::PxTransform parentJointLocalPoseInChild;
	if (paranthLink->getInboundJoint())
	{
		parentJointLocalPoseInChild = paranthLink->getInboundJoint()->getChildPose();
	}
	else
	{
		parentJointLocalPoseInChild = physx::PxTransform(physx::PxIdentity);
	}

	const physx::PxTransform parentGlobalPose = paranthLink->getGlobalPose();
	const physx::PxTransform childGlobalPose = childLink.getGlobalPose();
	const physx::PxTransform jointLocalPoseInChild = m_pxJoint->getChildPose();
	const physx::PxTransform parentJointGlobalPose =
		parentGlobalPose * parentJointLocalPoseInChild;
	const physx::PxTransform jointGlobalPose =
		childGlobalPose * jointLocalPoseInChild;

	math::vector3 parentPosition{};
	math::quaternion parentRotation{};
	PhysicsMath::FromPxTransform(
		parentJointGlobalPose, parentPosition, parentRotation);
	const math::matrix4x4 parentJointGlobalTransform = math::compose(
		math::vector3::one(), parentRotation, parentPosition);

	math::vector3 childPosition{};
	math::quaternion childRotation{};
	PhysicsMath::FromPxTransform(jointGlobalPose, childPosition, childRotation);
	const math::matrix4x4 childJointGlobalTransform = math::compose(
		math::vector3::one(), childRotation, childPosition);

	m_simulLocalTransform =
		childJointGlobalTransform * math::inverse(parentJointGlobalTransform);

	return true;
}
