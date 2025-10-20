#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Weapon.generated.h"
#include "ItemType.h"


class Player;
class Weapon : public ModuleBehavior
{
public:
   ReflectWeapon
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Weapon)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void SetEnabled(bool able);
	ItemType GetItemType() { return itemType; }
	//�⺻���� �˻�(������ ����)
	bool IsBasic() { return itemType == ItemType::Basic; }
	//������ ����
	void DecreaseDur(bool isCharge = false);
	//������ �˻���� ���� �޼���
	bool IsBroken() { return isBreak; }
	int GetCurDur() { return curDur; }
	int GetMaxDur() { return durMax; }

	bool CheckChargedDur(float chargedTime);

	int ItemTypeToInt() { return static_cast<int>(itemType); }
	ItemType IntToItemType(int type) { return static_cast<ItemType>(type); }

public:
	std::string itemName = "None";
	[[Property]]
	ItemType itemType = ItemType::Basic;

	//����
	[[Property]]
	int itemAckDmg = 1;      //�Ϲݰ��� ������
	[[Property]]
	float itemAckRange = 2.0f; //�Ϲݰ��� �����Ÿ�
	[[Property]]
	float itemKnockback = 0.05f; //�Ϲ� ���� �˹�Ÿ� (����)
	[[Property]]
	float coopCrit = 2.0f;   //�������� ������ ����
	
	//��¡����
	[[Property]]
	float chgTime = 0.2f; //Ű�ٿ� ������ n�ʴ� ������ ����
	[[Property]]
	int chgAckDmg = 5; //��¡ ���ݽ� ��������  // ���Ÿ������ �̰� Ư��ź ������?
	[[Property]]
	float chgRange = 4.0f; //��¡���� ��Ÿ�
	[[Property]]
	float chgKnockback = 0.05f; //��¡���� �˹�Ÿ�
	[[Property]]
	int ChargeAttackBulletCount = 5;  //�������ý� ���ư� źȯ��
	[[Property]] 
	int ChargeAttackBulletAngle = 15;  //�������ý� ���ư� źȯ ������ ����


	//������
	[[Property]]
	int durMax = 10; //�ִ� ������
	[[Property]]
	int durUseAtk = 1; //�Ϲ� ���ݽ� �Ҹ� ������
	int curDur = durMax;  //���� ������
	float chargingPersent = 0.f; //��¡���� �ð�/��¡�Ϸ�ð�
	bool isBreak = false;   //���� �μ���Ȯ�ο�
	bool isCompleteCharge = false; //��¡�Ϸ� Ȯ�ο�


	//��ź��
	[[Property]]
	float bombThrowDuration = 2.5f;
	[[Property]]
	float bombRadius = 2.5;
};
