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
// ����ڰ� ���ο� ������Ʈ�� �߰��� �� ������ ����� �ޱ� ���� ������Ʈ
class MonoBehavior : public IComponent, public ILifeSycle
{
public:
	virtual void Start() override {};
	virtual void Update(float tick) override {};
	virtual void FixedUpdate(float fixedTick) override {};
	virtual void LateUpdate(float tick) override {};

public:
	// ITriggerNotify��(��) ���� ��ӵ�
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};

	// ICollisionNotify��(��) ���� ��ӵ�
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};

public:
	// IComponent��(��) ���� ��ӵ�
	std::string ToString() const override = 0;
	uint32_t GetTypeID() const override = 0;
	uint32_t GetInstanceID() const override = 0;

};