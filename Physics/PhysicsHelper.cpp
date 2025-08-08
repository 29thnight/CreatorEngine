#include "PhysicsHelper.h"



void ConvertVectorPxToDx(const physx::PxVec3& pxVector, DirectX::SimpleMath::Vector3& dxVector)
{
	dxVector.x = pxVector.x;
	dxVector.y = pxVector.y;
	dxVector.z = -pxVector.z;
}


void ConvertQuaternionPxToDx(const physx::PxQuat& pxQuat, DirectX::SimpleMath::Quaternion& dxQuat)
{
	dxQuat.x = -pxQuat.x;
	dxQuat.y = -pxQuat.y;
	dxQuat.z = pxQuat.z; // PhysX는 오른손 좌표계, DirectX는 왼손 좌표계
	dxQuat.w = pxQuat.w;
}

void ConvertVectorDxToPx(const DirectX::SimpleMath::Vector3& dxVector, physx::PxVec3& pxVector)
{
	pxVector.x = dxVector.x;
	pxVector.y = dxVector.y;
	pxVector.z = -dxVector.z;
}


void ConvertQuaternionDxToPx(const DirectX::SimpleMath::Quaternion& dxQuat, physx::PxQuat& pxQuat) {
	pxQuat.x = -dxQuat.x;
	pxQuat.y = -dxQuat.y;
	pxQuat.z = dxQuat.z;
	pxQuat.w = dxQuat.w;
}

bool IsTransformDifferent(const physx::PxTransform& t1, const physx::PxTransform& t2, float epsilon)
{
	// 위치 비교
	if ((t1.p - t2.p).magnitudeSquared() > epsilon * epsilon) {
		return true;
	}

	// 회전 비교 (Dot Product 사용)
	// 두 쿼터니언이 같으면 내적(dot) 결과는 1 또는 -1
	if (abs(t1.q.dot(t2.q)) < 1.0f - epsilon) {
		return true;
	}

	return false;
}




