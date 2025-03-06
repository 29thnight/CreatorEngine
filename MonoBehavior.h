#pragma once
#include "Component.h"

struct ICollider;
// 사용자가 새로운 컴포넌트를 추가할 때 유용한 기능을 받기 위한 컴포넌트
class MonoBehavior : public Component
{
public:
	// Component을(를) 통해 상속됨
	virtual void Initialize() override = 0;
	virtual void FixedUpdate(float fixedTick) override = 0;
	virtual void Update(float tick) override = 0;
	virtual void LateUpdate(float tick) override = 0;
	virtual void EditorContext() override = 0;
	// 하위 객체들 미상속
	virtual void Serialize(_inout json& out) override = 0;
	virtual void DeSerialize(_in json& in) override = 0;

public:
	// ITriggerNotify을(를) 통해 상속됨
	virtual void OnTriggerEnter(ICollider* other) = 0;
	virtual void OnTriggerStay(ICollider* other) = 0;
	virtual void OnTriggerExit(ICollider* other) = 0;

	// ICollisionNotify을(를) 통해 상속됨
	virtual void OnCollisionEnter(ICollider* other) = 0;
	virtual void OnCollisionStay(ICollider* other) = 0;
	virtual void OnCollisionExit(ICollider* other) = 0;
	uint32 ID() override { return 0; }
};