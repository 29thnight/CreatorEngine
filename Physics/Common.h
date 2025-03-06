#pragma once
#include "PhysicsInfo.h"
#include "Physx.h"
#include "directxtk/SimpleMath.h"
#include "ICollider.h"
#include "IRigidbody.h"
#include <iostream>

using namespace DirectX::SimpleMath;
using namespace physx;

class IgnoreSelfQueryFilterCallback : public PxQueryFilterCallback {
public:
	PxRigidActor* ignoreActor; // 제외할 객체

	IgnoreSelfQueryFilterCallback(PxRigidActor* actor) : ignoreActor(actor) {}

	PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape,
		const PxRigidActor* actor, PxHitFlags& queryFlags) override;

	// PxQueryFilterCallback을(를) 통해 상속됨
	PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit,
		const PxShape* shape, const PxRigidActor* actor) override;

	// PxQueryFilterCallback을(를) 통해 상속됨
};

inline bool raycast(Vector3 origin, Vector3 direction, float distance, ICollider** hit) {

	physx::PxRaycastBuffer hitbuff;
	if (Physics.get()->GetPxScene()->raycast(
		PxVec3(origin.x, origin.y, origin.z),
		PxVec3(direction.x, direction.y, direction.z),
		distance, hitbuff))
	{
		if (hitbuff.hasBlock) {
			const physx::PxRaycastHit& raycasthit = hitbuff.block;
			//physx::PxRigidActor* actor = raycasthit.actor;
			physx::PxShape* shape = raycasthit.shape;
			ICollider* coll = static_cast<ICollider*>(shape->userData);

			if (coll != nullptr) {
				*hit = coll;
				return true;
			}
		}
	}
	return false;
}

inline ICollider* raycast(Vector3 origin, Vector3 direction, float distance) {
	physx::PxRaycastBuffer hitbuff;
	if (Physics.get()->GetPxScene()->raycast(
		PxVec3(origin.x, origin.y, origin.z),
		PxVec3(direction.x, direction.y, direction.z),
		distance, hitbuff))
	{
		if (hitbuff.hasBlock) {
			const physx::PxRaycastHit& raycasthit = hitbuff.block;
			//physx::PxRigidActor* actor = raycasthit.actor;
			physx::PxShape* shape = raycasthit.shape;
			ICollider* coll = static_cast<ICollider*>(shape->userData);

			if (coll != nullptr) {
				return coll;
			}
		}
	}
	return nullptr;
}

inline ICollider* raycast(Vector3 origin, Vector3 direction, float distance, PxQueryFilterData filterData, PxQueryFilterCallback* callback) {
	physx::PxRaycastBuffer hitbuff;
	if (Physics.get()->GetPxScene()->raycast(
		PxVec3(origin.x, origin.y, origin.z),
		PxVec3(direction.x, direction.y, direction.z),
		distance, hitbuff,PxHitFlag::eDEFAULT,
		filterData, callback))
	{
		if (hitbuff.hasBlock) {
			const physx::PxRaycastHit& raycasthit = hitbuff.block;
			//physx::PxRigidActor* actor = raycasthit.actor;
			physx::PxShape* shape = raycasthit.shape;
			ICollider* coll = static_cast<ICollider*>(shape->userData);

			if (coll != nullptr) {
				return coll;
			}
		}
	}
	return nullptr;
}

// hitCount만큼 반환. 없다면 -1.
inline int raycastAll(Vector3 origin, Vector3 direction, float distance, std::vector<ICollider*>& outColliders) {
	constexpr int MAX_HITS = 32; // 최대 충돌 개수
	physx::PxRaycastHit hitBuffer[MAX_HITS]; // 결과를 저장할 배열
	physx::PxRaycastBuffer raycastBuffer(hitBuffer, MAX_HITS);

	bool status = Physics.get()->GetPxScene()->raycast(
		PxVec3(origin.x, origin.y, origin.z),           // 시작점
		PxVec3(direction.x, direction.y, direction.z),  // 방향 (정규화된 벡터여야 함)
		distance,										// 최대 거리
		raycastBuffer,									// 결과 버퍼
		PxHitFlag::eDEFAULT								// 반환할 데이터 플래그
	);

	if (status) {
		int hitCount = raycastBuffer.getNbAnyHits();
		for (int i = 0; i < hitCount; i++) {
			const PxRaycastHit& hit = raycastBuffer.getAnyHit(i);

			ICollider* coll = static_cast<ICollider*>(hit.shape->userData);
			if (coll != nullptr)
				outColliders.push_back(coll);
		}
		return hitCount;
	}
	return -1;
}

inline int raycastAllResult(Vector3 origin, Vector3 direction, float distance, std::vector<HitInfo>& outColliders) {
	constexpr int MAX_HITS = 32; // 최대 충돌 개수
	physx::PxRaycastHit hitBuffer[MAX_HITS]; // 결과를 저장할 배열
	physx::PxRaycastBuffer raycastBuffer(hitBuffer, MAX_HITS);

	bool status = Physics.get()->GetPxScene()->raycast(
		PxVec3(origin.x, origin.y, origin.z),           // 시작점
		PxVec3(direction.x, direction.y, direction.z),  // 방향 (정규화된 벡터여야 함)
		distance,										// 최대 거리
		raycastBuffer,									// 결과 버퍼
		PxHitFlag::eDEFAULT								// 반환할 데이터 플래그
	);

	if (status) {
		int hitCount = raycastBuffer.getNbAnyHits();
		outColliders.reserve(hitCount);
		for (int i = 0; i < hitCount; i++) {
			const PxRaycastHit& hit = raycastBuffer.getAnyHit(i);

			HitInfo info;
			info.iCollider = static_cast<ICollider*>(hit.shape->userData);
			info.iRigidbody = static_cast<IRigidbody*>(hit.actor->userData);
			info.hitPosition[0] = hit.position.x;
			info.hitPosition[1] = hit.position.y;
			info.hitPosition[2] = hit.position.z;
			info.hitNormal[0] = hit.normal.x;
			info.hitNormal[1] = hit.normal.y;
			info.hitNormal[2] = hit.normal.z;

			outColliders.push_back(info);
		}
		return hitCount;
	}
	return -1;
}

inline bool RaycastPlane(const physx::Plane& plane, const Vector3& rayOrigin, const Vector3& rayDir, float& enter)
{
	PxPlane p{ plane.normal[0], plane.normal[1], plane.normal[2], plane.dist };

	// 법선 벡터와 광선 방향 벡터의 내적 계산
	float denominator = p.n.dot(PxVec3(rayDir.x, rayDir.y, rayDir.z));

	// 내적이 0이면 평면과 광선이 평행하므로 교차 없음
	if (fabs(denominator) < 1e-6f)
	{
		return false;
	}

	// t 값 계산 (교차 거리)
	enter = -(p.n.dot(PxVec3(rayOrigin.x, rayOrigin.y, rayOrigin.z)) + p.d) / denominator;

	// t 값이 음수이면 광선이 반대 방향이므로 교차 없음
	return enter >= 0;
}

class CharacterController;
class ContactReportCallback : public PxUserControllerHitReport {
public:
	PxVec3 groundNormal = PxVec3(0, 1, 0); // 기본 Normal (Y-Up)
	bool isGround = false;

private:
	CharacterController* otherController;
public:
	CharacterController* GetOtherController();

	void onShapeHit(const PxControllerShapeHit& hit) override;
	void onControllerHit(const PxControllersHit& hit) override;
	void onObstacleHit(const PxControllerObstacleHit& hit) override;
};
