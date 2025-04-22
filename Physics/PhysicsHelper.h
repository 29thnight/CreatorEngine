
#pragma once
//#include "../Utility_Framework/Core.Minimal.h"
#include <directxtk/SimpleMath.h>
#include <physx/PxPhysicsAPI.h>

//PhysX�� ������ ��ǥ�踦 ����մϴ�.
//GameEngine�� � ��ǥ�踦 ����ϴ����� ��
//DirectX�� �޼� ��ǥ�踦, OpenGL�� ������ ��ǥ�踦 ����մϴ�.
//���� �������� DirectX�� �Ѵٸ�, PhysX�� DirectX�� ��ǥ�谡 �ٸ��� ������ ��ȯ�� �ʿ��մϴ�.
//�׿� ���� PhysX�� DirectX�� ��ǥ�踦 ��ȯ�ϴ� �Լ��� �����մϴ�.

//PhysX -> DirectX Matrix
void CopyMatrixPxToDx(const physx::PxTransform & pxTransform, DirectX::SimpleMath::Matrix & dxMatrix);
void CopyMatrixXYZPxToDx(const physx::PxTransform& pxTransform, DirectX::SimpleMath::Matrix& dxMatrix);

//PhysX -> DirectX Vector
void CopyVectorPxToDx(const physx::PxVec3& pxVector, DirectX::SimpleMath::Vector3& dxVector);

//DirectX Matrix -> PhysX
void CopyMatrixDxToPx(const DirectX::SimpleMath::Matrix& dxMatrix, physx::PxTransform& pxTransform);
void CopyMatrixXYZDxToPx(const DirectX::SimpleMath::Matrix& dxMatrix, physx::PxTransform& pxTransform);

//DirectX Vector -> PhysX
void CopyVectorDxToPx(const DirectX::SimpleMath::Vector3& dxVector, physx::PxVec3& pxVector);