
#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include <physx/PxPhysicsAPI.h>

//PhysX는 오른손 좌표계를 사용합니다.
//GameEngine이 어떤 좌표계를 사용하는지는 모름
//DirectX는 왼손 좌표계를, OpenGL은 오른손 좌표계를 사용합니다.
//만약 렌더링을 DirectX로 한다면, PhysX와 DirectX의 좌표계가 다르기 때문에 변환이 필요합니다.
//그에 따라 PhysX와 DirectX의 좌표계를 변환하는 함수를 정의합니다.

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