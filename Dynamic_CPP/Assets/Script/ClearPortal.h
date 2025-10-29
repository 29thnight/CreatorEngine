#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class GameManager;
class ClearPortal : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ClearPortal)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}




	void SwitchScene();   //듀토일떈 스테이지로 스테이지에선 보스로 
	bool isPortalReady = false; //아시스가 특정지정 도착시 포탈 준비   // 준비되야 활성화가능
private:
	int  playerCount = 0;
	bool isSwitching{ false };
	GameManager* m_gameManager;

};
