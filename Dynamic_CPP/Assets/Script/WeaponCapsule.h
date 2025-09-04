#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Player;
//�ƽý��� ��ȭ�� ������ ����ĸ�� -> �÷��̾�� ���޽� ��Ī�� weapon ��������
class WeaponCapsule : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(WeaponCapsule)
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


	//�ƽý� -> �÷��̾� ���ƿË����°͵�
	int weaponCode = 0;  //asis�� �Ѱ��ٰ�  0 1 2 �������� �ٸ� �������
	void Throw(Player* _player, Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //���ư� ���ã�¿�
};
