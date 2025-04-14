#include "PhysicsHelper.h"

void CopyMatrixPxToDx(const physx::PxTransform & pxTransform, Mathf::Matrix & dxMatrix)
{
	Mathf::Vector3 translation = { pxTransform.p.x, pxTransform.p.y, pxTransform.p.z };
	Mathf::Quaternion rotation = { pxTransform.q.x, pxTransform.q.y, pxTransform.q.z, pxTransform.q.w };
	dxMatrix = DirectX::XMMatrixAffineTransformation(DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f), DirectX::XMVectorZero(), rotation, translation);
}

void CopyMatrixXYZPxToDx(const physx::PxTransform& pxTransform, Mathf::Matrix& dxMatrix)
{
	Mathf::Vector3 translation = { pxTransform.p.x, pxTransform.p.y, pxTransform.p.z };
	Mathf::Quaternion rotation = { pxTransform.q.x, pxTransform.q.y, pxTransform.q.z, pxTransform.q.w };
	Mathf::Vector3 scale = { 1.f, 1.f, 1.f };
	dxMatrix = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) * DirectX::XMMatrixRotationQuaternion(rotation) * DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);

}

void CopyVectorPxToDx(const physx::PxVec3& pxVector, Mathf::Vector3& dxVector)
{
	dxVector.x = pxVector.x;
	dxVector.y = pxVector.y;
	dxVector.z = pxVector.z;
}

void CopyMatrixDxToPx(const Mathf::Matrix& dxMatrix, physx::PxTransform& pxTransform)
{
	pxTransform.p.x = dxMatrix._41;
	pxTransform.p.y = dxMatrix._42;
	pxTransform.p.z = dxMatrix._43;

	DirectX::XMVECTOR rotation = DirectX::XMQuaternionRotationMatrix(dxMatrix);
	pxTransform.q.x = DirectX::XMVectorGetX(rotation);
	pxTransform.q.y = DirectX::XMVectorGetY(rotation);
	pxTransform.q.z = DirectX::XMVectorGetZ(rotation);
	pxTransform.q.w = DirectX::XMVectorGetW(rotation);

}

void CopyMatrixXYZDxToPx(const Mathf::Matrix& dxMatrix, physx::PxTransform& pxTransform)
{
	Mathf::Matrix copyMatrix = dxMatrix;

	Mathf::Vector3 position;
	Mathf::Quaternion rotation;
	Mathf::Vector3 scale;

	copyMatrix.Decompose(scale, rotation, position);

	pxTransform.p.x = position.x;
	pxTransform.p.y = position.y;
	pxTransform.p.z = position.z;
	pxTransform.q.x = rotation.x;
	pxTransform.q.y = rotation.y;
	pxTransform.q.z = rotation.z;
	pxTransform.q.w = rotation.w;

}

void CopyVectorDxToPx(const Mathf::Vector3& dxVector, physx::PxVec3& pxVector)
{
	pxVector.x = dxVector.x;
	pxVector.y = dxVector.y;
	pxVector.z = dxVector.z;
}