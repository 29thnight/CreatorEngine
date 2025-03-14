#pragma once
#ifndef _inout
#define _inout
#define _in
#endif

#ifndef interface
#define interface struct
#endif

#include "IComponent.h"

struct ICollider;
// 사용자가 새로운 컴포넌트를 추가할 때 유용한 기능을 받기 위한 컴포넌트
class MonoBehavior : public IComponent
{
public:
	// Component을(를) 통해 상속됨
	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};

public:
	// ITriggerNotify을(를) 통해 상속됨
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};

	// ICollisionNotify을(를) 통해 상속됨
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};
};