#include "PhysicsEventCallback.h"
#include "ICollider.h"
#include "Physx.h"

using namespace physx;

void PhysicsEventCallback::onConstraintBreak(PxConstraintInfo* constraints, PxU32 count)
{
}

void PhysicsEventCallback::onWake(PxActor** actors, PxU32 count)
{
}

void PhysicsEventCallback::onSleep(PxActor** actors, PxU32 count)
{
}

void PhysicsEventCallback::onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs)
{
	//std::cout << "onContact" << std::endl;
	//발생한 충동 이벤트 전체 이벤트 순회
	for (PxU32 i = 0; i < nbPairs; i++) {

		//OnCollisionEnter
		if (pairs[i].events & (physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_TOUCH_CCD))
		{
			SettingCollisionData(pairHeader, &pairs[i], ECollisionEventType::ENTER_COLLISION);
		}
		//OnCollisionExit
		else if (pairs[i].events & (physx::PxPairFlag::eNOTIFY_TOUCH_LOST | physx::PxPairFlag::eNOTIFY_TOUCH_CCD)) 
		{
			SettingCollisionData(pairHeader, &pairs[i], ECollisionEventType::END_COLLISION);
		}
		//OnCollisionStay
		else if (pairs[i].events & (physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS | physx::PxPairFlag::eNOTIFY_TOUCH_CCD)) 
		{
			SettingCollisionData(pairHeader, &pairs[i], ECollisionEventType::ON_COLLISION);
		}
	}
}

void PhysicsEventCallback::onTrigger(PxTriggerPair* pairs, PxU32 count)
{
	for (PxU32 i = 0; i < count; i++)
	{
		//Start OverLap 
		if (pairs[i].status & PxPairFlag::eNOTIFY_TOUCH_FOUND)
		{
			SettingTriggerData(&pairs[i], ECollisionEventType::ENTER_OVERLAP);
		}
		//End OverLap
		else if (pairs[i].status & PxPairFlag::eNOTIFY_TOUCH_LOST)
		{
			SettingTriggerData(&pairs[i], ECollisionEventType::END_OVERLAP);
		}
	}
}

void PhysicsEventCallback::onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count)
{
}

void PhysicsEventCallback::StartTrigger()
{
	//
	for (auto trigger = m_triggerMap.begin(); trigger != m_triggerMap.end();) {
		CollisionData* TriggerActorData = Physics->FindCollisionData(trigger->first);
		if (TriggerActorData == nullptr)
		{
			trigger = m_triggerMap.erase(m_triggerMap.find(trigger->first));
			continue;
		}

		for (auto otherTrigger = trigger->second.begin(); otherTrigger != trigger->second.end();)
		{
			CollisionData* OtherActorData = Physics->FindCollisionData(*otherTrigger);
			if (OtherActorData == nullptr)
			{
				otherTrigger = trigger->second.erase(trigger->second.find(*otherTrigger));
				continue;
			}


			CollisionData ThisData;
			CollisionData OtherData;

			ThisData.thisId = TriggerActorData->thisId;
			ThisData.otherId = OtherActorData->thisId;
			ThisData.thisLayerNumber = TriggerActorData->thisLayerNumber;
			ThisData.otherLayerNumber = OtherActorData->thisLayerNumber;

			OtherData.thisId = OtherActorData->thisId;
			OtherData.otherId = TriggerActorData->thisId;
			OtherData.thisLayerNumber = OtherActorData->thisLayerNumber;
			OtherData.otherLayerNumber = TriggerActorData->thisLayerNumber;

			m_callbackFunction(ThisData, ECollisionEventType::ON_OVERLAP);
			m_callbackFunction(OtherData, ECollisionEventType::ON_OVERLAP);

			++otherTrigger;
		}
		++trigger;
	}
}

void PhysicsEventCallback::SettingCollisionData(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, const ECollisionEventType& eventType)
{
	//물리엔진에서 제공하는 충돌 쌍의 충돌 지점 정보를 담는 벡터
	std::vector<physx::PxContactPairPoint> contactPoints;

	//받은 충돌 쌍
	const physx::PxContactPair& contactPair = pairs[0];
	//라이브러리에서 제공할 충돌 쌍의 충돌 지점 정보
	std::vector<DirectX::SimpleMath::Vector3> points;


	//충돌 쌍의 충돌 지점 정보의 갯수 만큼 벡터를 할당
	physx::PxU32 contactCount = contactPair.contactCount;
	contactPoints.resize(contactCount);
	points.resize(contactCount);
	
	//충돌 쌍의 충돌 지점 정보를 벡터에 담기
	//PxContactPairPoint
	if (contactCount > 0 )
	{
		contactPair.extractContacts(&contactPoints[0], contactCount);
	}
	//PxContactPairPoint->Vector3
	for (physx::PxU32 i = 0; i < contactCount; i++)
	{
		points[i].x = contactPoints[i].position.x;
		points[i].y = contactPoints[i].position.y;
		points[i].z = contactPoints[i].position.z;
	}

	//제공해줄 충돌 정보
	CollisionData firstActor;
	CollisionData secondActor;
	CollisionData* firstData = (CollisionData*)pairHeader.actors[0]->userData;
	CollisionData* secondData = (CollisionData*)pairHeader.actors[1]->userData;
	
	//충돌 정보가 유효한지 확인
	if (firstData == nullptr || secondData == nullptr)
	{
		return;
	}

	//충돌 정보에 충돌 지점 정보 담기
	
	//firstActor
	firstActor.thisId = firstData->thisId;
	firstActor.otherId = secondData->thisId;
	firstActor.thisLayerNumber = firstData->thisLayerNumber;
	firstActor.otherLayerNumber = secondData->thisLayerNumber;
	firstActor.contactPoints = points;

	//secondActor
	secondActor.thisId = secondData->thisId;
	secondActor.otherId = firstData->thisId;
	secondActor.thisLayerNumber = secondData->thisLayerNumber;
	secondActor.otherLayerNumber = firstData->thisLayerNumber;
	secondActor.contactPoints = points;

	//콜백 함수 호출
	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);

	//객체를 지워야 하는 경우 data의 isDead 플래그를 확인
	if (eventType == ECollisionEventType::END_COLLISION && firstData->isDead) {
		Physics->RemoveCollisionData(firstData->thisId);
	}
	if (eventType == ECollisionEventType::END_COLLISION && secondData->isDead) {
		Physics->RemoveCollisionData(secondData->thisId);
	}
}

void PhysicsEventCallback::SettingTriggerData(const physx::PxTriggerPair* pairs, const ECollisionEventType& eventType)
{
	CollisionData firstActor;
	CollisionData secondActor;
	CollisionData* firstData = (CollisionData*)pairs->triggerActor->userData;
	CollisionData* secondData = (CollisionData*)pairs->otherActor->userData;

	//충돌 정보가 유효한지 확인
	if (firstData == nullptr || secondData == nullptr)
	{
		return;
	}

	//충돌 정보에 충돌 지점 정보 담기
	//firstActor
	firstActor.thisId = firstData->thisId;
	firstActor.otherId = secondData->thisId;
	firstActor.thisLayerNumber = firstData->thisLayerNumber;
	firstActor.otherLayerNumber = secondData->thisLayerNumber;
	//secondActor
	secondActor.thisId = secondData->thisId;
	secondActor.otherId = firstData->thisId;
	secondActor.thisLayerNumber = secondData->thisLayerNumber;
	secondActor.otherLayerNumber = firstData->thisLayerNumber;

	//콜백 함수 호출
	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);

	//트리거 정보 저장
	CountTrigger(firstData->thisId, secondData->thisId, eventType);

	//객체를 지워야 하는 경우 data의 isDead 플래그를 확인
	if (eventType == ECollisionEventType::END_OVERLAP && firstData->isDead) {
		Physics->RemoveCollisionData(firstData->thisId);
	}

	if (eventType == ECollisionEventType::END_OVERLAP && secondData->isDead) {
		Physics->RemoveCollisionData(secondData->thisId);
	}

	
}

void PhysicsEventCallback::CountTrigger(unsigned int triggerId,unsigned int otherId,const ECollisionEventType& eventType)
{
	//다중 충돌을 위한 트리거 맵 세트
	
	//충돌이 시작된 경우
	if (eventType == ECollisionEventType::ENTER_OVERLAP) {
		auto iter = m_triggerMap.find(triggerId);

		//충돌에 대한 정보가 이미 있는 경우 otherId를 추가
		if (iter != m_triggerMap.end()) {
			iter->second.insert(otherId);
		}
		// 충돌에 대한 정보가 없을 경우 세로운 컨테이너로 정보를 트	리거맵에 추가
		else
		{
			std::set<unsigned int> newTriggerIdSet;
			newTriggerIdSet.insert(otherId);
			m_triggerMap.insert(std::make_pair(triggerId, newTriggerIdSet));
		}
	}
	//충돌이 끝난 경우
	else if (eventType == ECollisionEventType::END_OVERLAP) 
	{
		//트리거 맵에서 triggerId를 찾고 세트되어 있는 otherId를 찾는다
		auto triggerIter = m_triggerMap.find(triggerId);
		// 이미 소멸된 객체에 대한 END_OVERLAP 이벤트일 수 있습니다.
		if (triggerIter == m_triggerMap.end()) {
			return; // 이미 처리되었거나 유효하지 않은 ID이므로 무시
		}
		auto otherSetIter = triggerIter->second.find(otherId);
		
		//충돌이 끝난 객체만 지움
		if (otherSetIter != triggerIter->second.end()) {
			triggerIter->second.erase(otherSetIter);
		}
		
		//아무런 충돌이 없는 경우 트리거 멥에서 정보 삭제
		if (triggerIter->second.empty()) {
			m_triggerMap.erase(triggerIter);
		}
		
	}
	
}
