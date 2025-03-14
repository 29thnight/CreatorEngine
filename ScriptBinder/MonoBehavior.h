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
// ����ڰ� ���ο� ������Ʈ�� �߰��� �� ������ ����� �ޱ� ���� ������Ʈ
class MonoBehavior : public IComponent
{
public:
	// Component��(��) ���� ��ӵ�
	virtual void Initialize() {};
	virtual void FixedUpdate(float fixedTick) {};
	virtual void Update(float tick) {};
	virtual void LateUpdate(float tick) {};

public:
	// ITriggerNotify��(��) ���� ��ӵ�
	virtual void OnTriggerEnter(ICollider* other) {};
	virtual void OnTriggerStay(ICollider* other) {};
	virtual void OnTriggerExit(ICollider* other) {};

	// ICollisionNotify��(��) ���� ��ӵ�
	virtual void OnCollisionEnter(ICollider* other) {};
	virtual void OnCollisionStay(ICollider* other) {};
	virtual void OnCollisionExit(ICollider* other) {};
};