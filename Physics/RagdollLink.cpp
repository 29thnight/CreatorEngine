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

	//�� ������ ������ ���ϴ���? --> // ������������ �������� �������� �ʱ� ����
	Mathf::Matrix dxTransform = Mathf::Matrix::CreateFromQuaternion(rotation) *
		Mathf::Matrix::CreateTranslation(position);

	if (parentLink == nullptr) {
		//�θ� ���� ���
		dxTransform *= Mathf::Matrix::CreateRotationZ(3.14f);
		CopyMatrixDxToPx(dxTransform, pxLocalTransform);

		m_pxLink = pxArtriculation->createLink(nullptr, pxLocalTransform);
	}
	else {
		//�θ� �ִ� ���
		CopyMatrixDxToPx(dxTransform, pxLocalTransform);

		m_pxLink = pxArtriculation->createLink(parentLink->GetPxLink(), pxLocalTransform);
		m_myJoint->In
	}

	return true;
}
