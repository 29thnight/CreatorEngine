#pragma once
#include <physx/PxPhysicsAPI.h>
using namespace physx;

class PhysicsEventCallback : public PxSimulationEventCallback
{
public:
	PhysicsEventCallback();
	~PhysicsEventCallback();

	void Initialize();

	// PxSimulationEventCallback을(를) 통해 상속됨
	void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override;
	void onWake(PxActor** actors, PxU32 count) override;
	void onSleep(PxActor** actors, PxU32 count) override;
	void onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) override;
	void onTrigger(PxTriggerPair* pairs, PxU32 count) override;
	void onAdvance(const PxRigidBody* const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override;

	void StartTrigger();

	void SettingCollisionData(const physx::PxContactPairHeader& pair);

	void SettiingTriggerData();

	void CountTrigger();

	inline void SetCallbackFunction(std::function<void(CollisionData, ECollisionEventType)> callbackFunction)
	{
		m_callbackFunction = callbackFunction;
	}

private:
	std::function<void(CollisionData, ECollisionEventType)> m_callbackFunction;

	std::unordered_map<unsigned int, std::set<unsigned int>> m_triggerMap; //트리거 맵
};

