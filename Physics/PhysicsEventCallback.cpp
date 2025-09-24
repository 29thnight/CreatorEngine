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
	//�߻��� �浿 �̺�Ʈ ��ü �̺�Ʈ ��ȸ
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
	//������������ �����ϴ� �浹 ���� �浹 ���� ������ ��� ����
	std::vector<physx::PxContactPairPoint> contactPoints;

	//���� �浹 ��
	const physx::PxContactPair& contactPair = pairs[0];
	//���̺귯������ ������ �浹 ���� �浹 ���� ����
	std::vector<DirectX::SimpleMath::Vector3> points;


	//�浹 ���� �浹 ���� ������ ���� ��ŭ ���͸� �Ҵ�
	physx::PxU32 contactCount = contactPair.contactCount;
	contactPoints.resize(contactCount);
	points.resize(contactCount);
	
	//�浹 ���� �浹 ���� ������ ���Ϳ� ���
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

	//�������� �浹 ����
	CollisionData firstActor;
	CollisionData secondActor;
	CollisionData* firstData = (CollisionData*)pairHeader.actors[0]->userData;
	CollisionData* secondData = (CollisionData*)pairHeader.actors[1]->userData;
	
	//�浹 ������ ��ȿ���� Ȯ��
	if (firstData == nullptr || secondData == nullptr)
	{
		return;
	}

	//�浹 ������ �浹 ���� ���� ���
	
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

	//�ݹ� �Լ� ȣ��
	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);

	//��ü�� ������ �ϴ� ��� data�� isDead �÷��׸� Ȯ��
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

	//�浹 ������ ��ȿ���� Ȯ��
	if (firstData == nullptr || secondData == nullptr)
	{
		return;
	}

	//�浹 ������ �浹 ���� ���� ���
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

	//�ݹ� �Լ� ȣ��
	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);

	//Ʈ���� ���� ����
	CountTrigger(firstData->thisId, secondData->thisId, eventType);

	//��ü�� ������ �ϴ� ��� data�� isDead �÷��׸� Ȯ��
	if (eventType == ECollisionEventType::END_OVERLAP && firstData->isDead) {
		Physics->RemoveCollisionData(firstData->thisId);
	}

	if (eventType == ECollisionEventType::END_OVERLAP && secondData->isDead) {
		Physics->RemoveCollisionData(secondData->thisId);
	}

	
}

void PhysicsEventCallback::CountTrigger(unsigned int triggerId,unsigned int otherId,const ECollisionEventType& eventType)
{
	//���� �浹�� ���� Ʈ���� �� ��Ʈ
	
	//�浹�� ���۵� ���
	if (eventType == ECollisionEventType::ENTER_OVERLAP) {
		auto iter = m_triggerMap.find(triggerId);

		//�浹�� ���� ������ �̹� �ִ� ��� otherId�� �߰�
		if (iter != m_triggerMap.end()) {
			iter->second.insert(otherId);
		}
		// �浹�� ���� ������ ���� ��� ���ο� �����̳ʷ� ������ Ʈ	���Ÿʿ� �߰�
		else
		{
			std::set<unsigned int> newTriggerIdSet;
			newTriggerIdSet.insert(otherId);
			m_triggerMap.insert(std::make_pair(triggerId, newTriggerIdSet));
		}
	}
	//�浹�� ���� ���
	else if (eventType == ECollisionEventType::END_OVERLAP) 
	{
		//Ʈ���� �ʿ��� triggerId�� ã�� ��Ʈ�Ǿ� �ִ� otherId�� ã�´�
		auto triggerIter = m_triggerMap.find(triggerId);
		// �̹� �Ҹ�� ��ü�� ���� END_OVERLAP �̺�Ʈ�� �� �ֽ��ϴ�.
		if (triggerIter == m_triggerMap.end()) {
			return; // �̹� ó���Ǿ��ų� ��ȿ���� ���� ID�̹Ƿ� ����
		}
		auto otherSetIter = triggerIter->second.find(otherId);
		
		//�浹�� ���� ��ü�� ����
		if (otherSetIter != triggerIter->second.end()) {
			triggerIter->second.erase(otherSetIter);
		}
		
		//�ƹ��� �浹�� ���� ��� Ʈ���� �㿡�� ���� ����
		if (triggerIter->second.empty()) {
			m_triggerMap.erase(triggerIter);
		}
		
	}
	
}
