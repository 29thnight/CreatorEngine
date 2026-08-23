#pragma once
//#include "../Utility_Framework/Core.Minimal.h"
#include <directxtk/SimpleMath.h>
#include <physx/PxPhysicsAPI.h>
#include <physx/foundation/PxTransform.h>


//PhysX는 오른손 좌표계를 사용합니다.
//GameEngine이 어떤 좌표계를 사용하는지는 모름
//DirectX는 왼손 좌표계를, OpenGL은 오른손 좌표계를 사용합니다.
//만약 렌더링을 DirectX로 한다면, PhysX와 DirectX의 좌표계가 다르기 때문에 변환이 필요합니다.
//그에 따라 PhysX와 DirectX의 좌표계를 변환하는 함수를 정의합니다.



//PhysX -> DirectX Vector
void ConvertVectorPxToDx(const physx::PxVec3& pxVec, DirectX::SimpleMath::Vector3& dxVec);
void ConvertQuaternionPxToDx(const physx::PxQuat & pxQuat, DirectX::SimpleMath::Quaternion& dxQuat);

//DirectX Vector -> PhysX
void ConvertVectorDxToPx(const DirectX::SimpleMath::Vector3& dxVec, physx::PxVec3& pxVec);
void ConvertQuaternionDxToPx(const DirectX::SimpleMath::Quaternion& dxQuat, physx::PxQuat& pxQuat);


bool IsTransformDifferent(const physx::PxTransform& t1, const physx::PxTransform& t2, float epsilon = 0.0001f);
