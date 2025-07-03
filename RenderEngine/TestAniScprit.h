#pragma once
#include "Component.h"
#include "TestAniScprit.generated.h"
class TestAniScprit :public Component
{
public:
   ReflectTestAniScprit
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(TestAniScprit)

	/*virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void OnTriggerEnter(const Collision& collider) override;
	virtual void OnTriggerStay(const Collision& collider) override;
	virtual void OnTriggerExit(const Collision& collider) override;
	virtual void OnCollisionEnter(const Collision& collider) override;
	virtual void OnCollisionStay(const Collision& collider) override;
	virtual void OnCollisionExit(const Collision& collider) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;*/
	[[Method]]
	void OnPunch();

	[[Method]]
	void Moving();

};

