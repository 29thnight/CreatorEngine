#pragma once
#include <tuple>
#include "PhysicsInfo.h"
class Object;
struct ICollider
{
	virtual void SetLocalPosition(std::tuple<float, float, float> pos) = 0;
	virtual void SetRotation(std::tuple<float, float, float, float> rotation) = 0;

	virtual void SetIsTrigger(bool isTrigger) = 0;
	virtual bool GetIsTrigger() = 0;

	virtual void OnTriggerEnter(ICollider* other) = 0;
	virtual void OnTriggerStay(ICollider* other) = 0;
	virtual void OnTriggerExit(ICollider* other) = 0;

	virtual void OnCollisionEnter(ICollider* other) = 0;
	virtual void OnCollisionStay(ICollider* other) = 0;
	virtual void OnCollisionExit(ICollider* other) = 0;

	virtual Object* GetOwner() = 0;
};

