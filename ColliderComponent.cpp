#include "ColliderComponent.h"
#include "Physics/Common.h"
#include "Object.h"
#include "RigidbodyComponent.h"
#include <iostream>
#include "MonoBehavior.h"

ColliderComponent::ColliderComponent()
{
	
}

ColliderComponent::~ColliderComponent()
{
	if (rigidbody != nullptr)
		rigidbody->rbActor->detachShape(*mShape);

	mShape->release();
	mShape = nullptr;
}

void ColliderComponent::Initialize()
{
	auto rb = _owner->GetComponent<RigidbodyComponent>();
	if (rb != nullptr)
	{
		rigidbody = rb;
		if (mShape != nullptr) {
			rb->SetCollider(this);
			//rigidbody->rbActor->attachShape(*mShape);

			SetEnable(info.enable);
			SetFilter();
		}
	}

	_id = 4002;
}

void ColliderComponent::OnTriggerEnter(ICollider* other)
{
	_owner->OnTriggerEnter(other);
	//std::cout << "OnTriggerEnter" << std::endl;
	auto notify = _owner->GetComponents<MonoBehavior>();
	for (auto& n : notify) {
		n->OnTriggerEnter(other);
	}
}

void ColliderComponent::OnTriggerStay(ICollider* other)
{
	//std::cout << "OnTriggerStay" << std::endl;
}

void ColliderComponent::OnTriggerExit(ICollider* other)
{
	_owner->OnTriggerExit(other);
	//std::cout << "OnTriggerExit" << std::endl;
	auto notify = _owner->GetComponents<MonoBehavior>();
	for (auto& n : notify) {
		n->OnTriggerExit(other);
	}
}

void ColliderComponent::OnCollisionEnter(ICollider* other)
{
	_owner->OnCollisionEnter(other);
	//std::cout << "OnCollisionEnter" << std::endl;
	auto notify = _owner->GetComponents<MonoBehavior>();
	for (auto& n : notify) {
		n->OnCollisionEnter(other);
	}
}

void ColliderComponent::OnCollisionStay(ICollider* other)
{
	_owner->OnCollisionStay(other);
	//std::cout << "OnCollisionStay" << std::endl;
	auto notify = _owner->GetComponents<MonoBehavior>();
	for (auto& n : notify) {
		n->OnCollisionStay(other);
	}
}

void ColliderComponent::OnCollisionExit(ICollider* other)
{
	_owner->OnCollisionExit(other);
	//std::cout << "OnCollisionExit" << std::endl;
	auto notify = _owner->GetComponents<MonoBehavior>();
	for (auto& n : notify) {
		n->OnCollisionExit(other);
	}
}

void ColliderComponent::SetEnable(bool enable)
{
	info.enable = enable;
	if (enable == false) {
		mShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		mShape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
	}
	else {
		SetIsTrigger(info.isTrigger);
	}
}

void ColliderComponent::SetFilter()
{
	PxFilterData filterData;
	filterData.word0 = info.flag;
	mShape->setQueryFilterData(filterData);
	PxFilterData similData;
	similData.word0 = info.simulFlag;
	mShape->setSimulationFilterData(similData);

}

Object* ColliderComponent::GetOwner()
{
	return _owner;
}

void ColliderComponent::FixedUpdate(float fixedTick)
{
}

void ColliderComponent::LateUpdate(float tick)
{
}

void ColliderComponent::SetLocalPosition(std::tuple<float, float, float> pos)
{
	PxTransform currentTransform = mShape->getLocalPose();
	auto [posX, posY, posZ] = pos;
	mShape->setLocalPose(PxTransform(PxVec3(posX, posY, posZ), currentTransform.q));
}

void ColliderComponent::SetRotation(std::tuple<float, float, float, float> rotation)
{
	PxTransform currentTransform = mShape->getLocalPose();
	auto [rotX, rotY, rotZ, rotW] = rotation;
	PxQuat pxRot = PxQuat(rotX, rotY, rotZ, rotW);
	PxTransform newLocalTransform(currentTransform.p, pxRot);
	mShape->setLocalPose(newLocalTransform);
}

void ColliderComponent::SetIsTrigger(bool isTrigger)
{
	if (info.enable == false) {
		mShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
		mShape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
	}
	else {
		if (isTrigger == true)
		{
			mShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); //충돌 반응 비활성화 
			mShape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true); // 트리거 설정
		}
		else
		{
			mShape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, false); // 트리거 설정
			mShape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true); //충돌 반응 활성화 
		}
	}
	info.isTrigger = isTrigger;
}

bool ColliderComponent::GetIsTrigger()
{
	return info.isTrigger;
}
