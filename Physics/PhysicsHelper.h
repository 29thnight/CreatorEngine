#pragma once
//#include "../Utility_Framework/Core.Minimal.h"
#include <directxtk/SimpleMath.h>
#include <physx/PxPhysicsAPI.h>
#include <physx/foundation/PxTransform.h>


//PhysX�� ������ ��ǥ�踦 ����մϴ�.
//GameEngine�� � ��ǥ�踦 ����ϴ����� ��
//DirectX�� �޼� ��ǥ�踦, OpenGL�� ������ ��ǥ�踦 ����մϴ�.
//���� �������� DirectX�� �Ѵٸ�, PhysX�� DirectX�� ��ǥ�谡 �ٸ��� ������ ��ȯ�� �ʿ��մϴ�.
//�׿� ���� PhysX�� DirectX�� ��ǥ�踦 ��ȯ�ϴ� �Լ��� �����մϴ�.



//PhysX -> DirectX Vector
void ConvertVectorPxToDx(const physx::PxVec3& pxVec, DirectX::SimpleMath::Vector3& dxVec);
void ConvertQuaternionPxToDx(const physx::PxQuat & pxQuat, DirectX::SimpleMath::Quaternion& dxQuat);

//DirectX Vector -> PhysX
void ConvertVectorDxToPx(const DirectX::SimpleMath::Vector3& dxVec, physx::PxVec3& pxVec);
void ConvertQuaternionDxToPx(const DirectX::SimpleMath::Quaternion& dxQuat, physx::PxQuat& pxQuat);


bool IsTransformDifferent(const physx::PxTransform& t1, const physx::PxTransform& t2, float epsilon = 0.0001f);
