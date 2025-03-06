#include "pch.h"
#include "PhysicsEventCallback.h"
#include "IRigidbody.h"
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
	const PxU32 buff = 64;
	PxContactPairPoint contacts[buff];

	PxActor* thisActor = pairHeader.actors[0];
	PxActor* otherActor = pairHeader.actors[1];

	for (PxU32 i = 0; i < nbPairs; i++) {
		const PxContactPair& curContactPair = pairs[i];
		PxU32 nbContacts = curContactPair.extractContacts(contacts, buff);

		ICollider* collL = static_cast<ICollider*>(curContactPair.shapes[0]->userData);
		ICollider* collR = static_cast<ICollider*>(curContactPair.shapes[1]->userData);

		for (PxU32 j = 0; j < nbContacts; j++) {
			PxVec3 point = contacts[j].position;
		}

		if (curContactPair.events == PxPairFlag::eNOTIFY_TOUCH_FOUND) {
			collL->OnCollisionEnter(collR);
			collR->OnCollisionEnter(collL);
		}
		else if (curContactPair.events == PxPairFlag::eNOTIFY_TOUCH_PERSISTS) {
			collL->OnCollisionStay(collR);
			collR->OnCollisionStay(collL);
		}
		else if (curContactPair.events == PxPairFlag::eNOTIFY_TOUCH_LOST) {
			collL->OnCollisionExit(collR);
			collR->OnCollisionExit(collL);
		}
	}
}

void PhysicsEventCallback::onTrigger(PxTriggerPair* pairs, PxU32 count)
{
	Physics->ShowNotRelease();
	for (PxU32 i = 0; i < count; i++) {
		const PxTriggerPair& curTriggerPair = pairs[i];

		PxShape* triggerShape = curTriggerPair.triggerShape;
		PxShape* otherShape = curTriggerPair.otherShape;
		ICollider* collL = static_cast<ICollider*>(triggerShape->userData);
		ICollider* collR = static_cast<ICollider*>(otherShape->userData);

		if (curTriggerPair.status == PxPairFlag::eNOTIFY_TOUCH_FOUND) {
			collL->OnTriggerEnter(collR);
			collR->OnTriggerEnter(collL);
		}
		// 5.0 이후로 트리거의 접촉중을 지원하지 않음.
		//else if (curTriggerPair.status == PxPairFlag::eNOTIFY_TOUCH_PERSISTS) {
		//	collL->OnTriggerStay(collR);
		//	collR->OnTriggerStay(collL);
		//}
		else if (curTriggerPair.status == PxPairFlag::eNOTIFY_TOUCH_LOST) {
			collL->OnTriggerExit(collR);
			collR->OnTriggerExit(collL);
		}
	}
}

void PhysicsEventCallback::onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count)
{
}
