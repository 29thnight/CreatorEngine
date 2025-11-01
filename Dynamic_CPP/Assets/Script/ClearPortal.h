#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class GameManager;
class EffectComponent;
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

	void TriggerPortal();

	EffectComponent* UnReadyPortal = nullptr; //�ƽý��� ���Դ´� ���� �Ѵ� �ȿö������
	EffectComponent* ReadyPortal = nullptr; //�Ѵ� �ö������
	EffectComponent* m_portalEffect = nullptr;

	void SwitchScene();   //�����ϋ� ���������� ������������ ������ 
	bool isPortalReady = false; //�ƽý��� Ư������ ������ ��Ż �غ�   // �غ�Ǿ� Ȱ��ȭ����
	bool InPortal2 = false;
	bool preInPortal2 = false;
private:
	int  playerCount = 0;
	bool isSwitching{ false };
	GameManager* m_gameManager;

};
