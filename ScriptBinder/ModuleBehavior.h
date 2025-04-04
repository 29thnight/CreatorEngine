#pragma once
#ifndef _inout
#define _inout
#define _in
#endif

#ifndef interface
#define interface struct
#endif

#include "Component.h"
#include "ILifeSycle.h"

struct ICollider;
// ����ڰ� ���ο� ������Ʈ�� �߰��� �� ������ ����� �ޱ� ���� ������Ʈ
class ModuleBehavior : public Component, public ILifeSycle
{
public:
	ModuleBehavior()
	{
	   m_orderID = Component::Order2Uint(ComponentOrder::BehaviorScript);
	   m_typeID = TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>();
	}
	virtual ~ModuleBehavior()
	{
	}

public:
	virtual void Start() override {};
	virtual void FixedUpdate(float fixedTick) override {};
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};
	virtual void Update(float tick) override {};
	virtual void LateUpdate(float tick) override {};

public:
	// IComponent��(��) ���� ��ӵ�
	std::string ToString() const override = 0;
};