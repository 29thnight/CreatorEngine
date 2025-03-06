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

	if (hit.worldNormal.y > 0.5f) { // ���� �������� �浹�� ��� = �ٴ�
		groundNormal = hit.worldNormal; // �ٴ��� Normal ����
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
		return PxQueryHitType::eNONE; // ����
	}
	if (filterData.word0 & shape->getQueryFilterData().word0) {
		return PxQueryHitType::eBLOCK; // �浹 ���
	}
	return PxQueryHitType::eNONE; // ����
}

PxQueryHitType::Enum IgnoreSelfQueryFilterCallback::postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor)
{
	std::cout << "Test2 raycast post" << std::endl;

	// ex) �浹�ϰ� ���� 0001 & 1111�̸� 0001�̹Ƿ� true
	// ex) �浹�ϰ� ���� 0001 & 1110�̸� 0000�̹Ƿ� false
	if (filterData.word0 & shape->getQueryFilterData().word0) {
		return PxQueryHitType::eBLOCK;
	}
	return PxQueryHitType::eNONE; // ����
}