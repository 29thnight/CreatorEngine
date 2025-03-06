#pragma once
#include "Component.h"

struct ICollider;
// ����ڰ� ���ο� ������Ʈ�� �߰��� �� ������ ����� �ޱ� ���� ������Ʈ
class MonoBehavior : public Component
{
public:
	// Component��(��) ���� ��ӵ�
	virtual void Initialize() override = 0;
	virtual void FixedUpdate(float fixedTick) override = 0;
	virtual void Update(float tick) override = 0;
	virtual void LateUpdate(float tick) override = 0;
	virtual void EditorContext() override = 0;
	// ���� ��ü�� �̻��
	virtual void Serialize(_inout json& out) override = 0;
	virtual void DeSerialize(_in json& in) override = 0;

public:
	// ITriggerNotify��(��) ���� ��ӵ�
	virtual void OnTriggerEnter(ICollider* other) = 0;
	virtual void OnTriggerStay(ICollider* other) = 0;
	virtual void OnTriggerExit(ICollider* other) = 0;

	// ICollisionNotify��(��) ���� ��ӵ�
	virtual void OnCollisionEnter(ICollider* other) = 0;
	virtual void OnCollisionStay(ICollider* other) = 0;
	virtual void OnCollisionExit(ICollider* other) = 0;
	uint32 ID() override { return 0; }
};