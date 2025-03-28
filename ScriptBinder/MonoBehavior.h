#pragma once
#ifndef _inout
#define _inout
#define _in
#endif

#ifndef interface
#define interface struct
#endif

#include "IComponent.h"
#include "ILifeSycle.h"

struct ICollider;
// 사용자가 새로운 컴포넌트를 추가할 때 유용한 기능을 받기 위한 컴포넌트
class MonoBehavior : public IComponent, public ILifeSycle
{
public:
	virtual void Start() override {};
	virtual void Update(float tick) override {};
	virtual void FixedUpdate(float fixedTick) override {};
	virtual void LateUpdate(float tick) override {};

public:
	// ITriggerNotify을(를) 통해 상속됨
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};

	// ICollisionNotify을(를) 통해 상속됨
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};

public:
	// IComponent을(를) 통해 상속됨
	std::string ToString() const override = 0;
	uint32_t GetTypeID() const override = 0;
	uint32_t GetInstanceID() const override = 0;

};