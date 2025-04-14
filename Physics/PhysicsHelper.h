
#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include <physx/PxPhysicsAPI.h>

//PhysX�� ������ ��ǥ�踦 ����մϴ�.
//GameEngine�� � ��ǥ�踦 ����ϴ����� ��
//DirectX�� �޼� ��ǥ�踦, OpenGL�� ������ ��ǥ�踦 ����մϴ�.
//���� �������� DirectX�� �Ѵٸ�, PhysX�� DirectX�� ��ǥ�谡 �ٸ��� ������ ��ȯ�� �ʿ��մϴ�.
//�׿� ���� PhysX�� DirectX�� ��ǥ�踦 ��ȯ�ϴ� �Լ��� �����մϴ�.

//PhysX -> DirectX Matrix
void CopyMatrixPxToDx(const physx::PxTransform & pxTransform, Mathf::Matrix & dxMatrix);
void CopyMatrixXYZPxToDx(const physx::PxTransform& pxTransform, Mathf::Matrix& dxMatrix);

//PhysX -> DirectX Vector
void CopyVectorPxToDx(const physx::PxVec3& pxVector, Mathf::Vector3& dxVector);

//DirectX Matrix -> PhysX
void CopyMatrixDxToPx(const Mathf::Matrix& dxMatrix, physx::PxTransform& pxTransform);
void CopyMatrixXYZDxToPx(const Mathf::Matrix& dxMatrix, physx::PxTransform& pxTransform);

//DirectX Vector -> PhysX
void CopyVectorDxToPx(const Mathf::Vector3& dxVector, physx::PxVec3& pxVector);