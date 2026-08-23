#include "RagdollJoint.h"

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

	DirectX::SimpleMath::Vector3 scale;
	DirectX::SimpleMath::Quaternion rotation;
	DirectX::SimpleMath::Vector3 position;
	m_localTransform.Decompose(scale, rotation, position);
	m_localTransform = DirectX::SimpleMath::Matrix::CreateScale(scale) * DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) * DirectX::SimpleMath::Matrix::CreateTranslation(position);

	physx::PxTransform pxLocalTransform;
	ConvertVectorDxToPx(position, pxLocalTransform.p);
	ConvertQuaternionDxToPx(rotation, pxLocalTransform.q);
	
	//CopyMatrixDxToPx(m_localTransform, pxLocalTransform);
	m_pxJoint->setChildPose(pxLocalTransform);

	DirectX::SimpleMath::Matrix parentLocalTransform = m_localTransform * ownerLink->GetLocalTransform();
	physx::PxTransform pxParentLocalTransform;
	DirectX::SimpleMath::Vector3 parentscale;
	DirectX::SimpleMath::Quaternion parentrotation;
	DirectX::SimpleMath::Vector3 parentposition;
	m_localTransform.Decompose(parentscale, parentrotation, parentposition);
	
	ConvertVectorDxToPx(parentposition, pxParentLocalTransform.p);
	ConvertQuaternionDxToPx(parentrotation, pxParentLocalTransform.q);


	//CopyMatrixDxToPx(parentLocalTransform, pxLocalTransform);
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

	//부모 링크의 조인트가 있는 경우
	if (paranthLink->getInboundJoint())
	{
		//부모 링크의 조인트의 로컬 포즈를 가져옴
		parentJointLocalPoseInChild = paranthLink->getInboundJoint()->getChildPose();
	}
	else {
		//아닌 경우 0으로 초기화
		parentJointLocalPoseInChild = physx::PxTransform(physx::PxIdentity);
	}

	//부모 자식 개체의 글로벌 포즈를 가져옴
	physx::PxTransform parentGlobalPose = paranthLink->getGlobalPose();
	physx::PxTransform childGlobalPose = childLink.getGlobalPose();

	//현제 조인트의 자식 개체의 로컬 포즈를 가져옴
	physx::PxTransform JointLocalPoseInChild = m_pxJoint->getChildPose();

	//자식 개체의 로컬 포즈를 글로벌 포즈로 변환
	physx::PxTransform parentJointGlobalPose = parentGlobalPose * parentJointLocalPoseInChild;
	physx::PxTransform JointGlobalPose = childGlobalPose * JointLocalPoseInChild;

	//physX global 포즈를 dx 트렌스폼으로 변환
	DirectX::SimpleMath::Matrix dxParentJointGlobalTransform;
	DirectX::SimpleMath::Matrix dxChildJointGlobalTransform;
	{
		DirectX::SimpleMath::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		DirectX::SimpleMath::Quaternion rotation;
		DirectX::SimpleMath::Vector3 position;
		ConvertVectorPxToDx(parentJointGlobalPose.p, position);
		ConvertQuaternionPxToDx(parentJointGlobalPose.q, rotation);
		dxParentJointGlobalTransform = DirectX::SimpleMath::Matrix::CreateScale(scale) *
			DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) *
			DirectX::SimpleMath::Matrix::CreateTranslation(position);
	}

	{
		DirectX::SimpleMath::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		DirectX::SimpleMath::Quaternion rotation;
		DirectX::SimpleMath::Vector3 position;
		ConvertVectorPxToDx(JointGlobalPose.p, position);
		ConvertQuaternionPxToDx(JointGlobalPose.q, rotation);
		dxChildJointGlobalTransform = DirectX::SimpleMath::Matrix::CreateScale(scale) *
			DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) *
			DirectX::SimpleMath::Matrix::CreateTranslation(position);
	}

	//CopyMatrixPxToDx(parentJointGlobalPose, dxParentJointGlobalTransform);
	//CopyMatrixPxToDx(JointGlobalPose, dxChildJointGlobalTransform);

	//부모 조인트의 인버트 트랜스폼으로 자식 조인트의 로커 트랜스폼 계산
	m_simulLocalTransform = dxChildJointGlobalTransform * dxParentJointGlobalTransform.Invert();

	return true;
}
