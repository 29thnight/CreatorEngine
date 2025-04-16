#include "RagdollLink.h"

bool RagdollLink::Initialize(const LinkInfo& linkInfo, RagdollLink* parentLink, physx::PxArticulationReducedCoordinate* pxArtriculation)
{
	m_name = linkInfo.boneName;
	m_density = linkInfo.density;
	m_localTransform = linkInfo.localTransform;
	m_parentLink = parentLink;

	physx::PxTransform pxLocalTransform;
	Mathf::Vector3 scale;
	Mathf::Quaternion rotation;
	Mathf::Vector3 position;
	m_localTransform.Decompose(scale, rotation, position);

	//왜 스케일 적용을 안하는지? --> // 물리엔진에서 스케일을 적용하지 않기 때문
	Mathf::Matrix dxTransform = Mathf::Matrix::CreateFromQuaternion(rotation) *
		Mathf::Matrix::CreateTranslation(position);

	if (parentLink == nullptr) {
		//부모가 없는 경우
		dxTransform *= Mathf::Matrix::CreateRotationZ(3.14f);
		CopyMatrixDxToPx(dxTransform, pxLocalTransform);

		m_pxLink = pxArtriculation->createLink(nullptr, pxLocalTransform);
	}
	else {
		//부모가 있는 경우
		CopyMatrixDxToPx(dxTransform, pxLocalTransform);

		m_pxLink = pxArtriculation->createLink(parentLink->GetPxLink(), pxLocalTransform);
		m_myJoint->In
	}

	return true;
}
