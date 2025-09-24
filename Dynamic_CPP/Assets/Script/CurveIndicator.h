#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class CurveIndicator : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(CurveIndicator)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void SetIndicator(Mathf::Vector3 start, Mathf::Vector3 end, float height);
	void EnableIndicator(bool enable);

private:
	Mathf::Vector3 startPos;
	Mathf::Vector3 endPos;
	float heightPos = 0.f;

private:
	std::vector<GameObject*> indicators;
	std::vector<Mathf::Vector3> indicatorInitScale;
	bool enableIndicator = false;
	float indicatorMinLimitSize = 0.7f;
	float indicatorInterval = 1.0f;
};

/*
	indicatorCount  :  자식으로 들어간 object의 갯수
*/
