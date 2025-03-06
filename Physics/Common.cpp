#include "pch.h"
#include "Common.h"

CharacterController* ContactReportCallback::GetOtherController()
{
	auto temp = otherController;
	otherController = nullptr;
	return temp;
}

void ContactReportCallback::onShapeHit(const PxControllerShapeHit& hit)
{
	isGround = false;

	if (hit.worldNormal.y > 0.5f) { // 위쪽 방향으로 충돌한 경우 = 바닥
		groundNormal = hit.worldNormal; // 바닥의 Normal 저장
		isGround = true;
	}
}

void ContactReportCallback::onControllerHit(const PxControllersHit& hit)
{
	otherController = static_cast<CharacterController*>(hit.other->getUserData());
}

void ContactReportCallback::onObstacleHit(const PxControllerObstacleHit& hit)
{
}

PxQueryHitType::Enum IgnoreSelfQueryFilterCallback::preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags)
{
	std::cout << "Test raycast pre" << std::endl;

	if (actor == ignoreActor) {
		return PxQueryHitType::eNONE; // 무시
	}
	if (filterData.word0 & shape->getQueryFilterData().word0) {
		return PxQueryHitType::eBLOCK; // 충돌 허용
	}
	return PxQueryHitType::eNONE; // 무시
}

PxQueryHitType::Enum IgnoreSelfQueryFilterCallback::postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor)
{
	std::cout << "Test2 raycast post" << std::endl;

	// ex) 충돌하고 싶은 0001 & 1111이면 0001이므로 true
	// ex) 충돌하고 싶은 0001 & 1110이면 0000이므로 false
	if (filterData.word0 & shape->getQueryFilterData().word0) {
		return PxQueryHitType::eBLOCK;
	}
	return PxQueryHitType::eNONE; // 무시
}