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

	virtual void Attack(Player* _Owner, AttackContext _attackContext = {}) {}
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
	float itemAckSpd = 1.0f; //�Ϲݰ��� �ӵ�
	[[Property]]
	float itemAckRange = 2.0f; //�Ϲݰ��� �����Ÿ�
	[[Property]]
	float itemKnockback = 0.5f; //�Ϲ� ���� �˹�Ÿ� (����)
	[[Property]]
	float coopCrit = 2.0f;   //�������� ������ ����
	
	//��¡����
	[[Property]]
	float chgTime = 0.2f; //Ű�ٿ� ������ n�ʴ� ������ ����
	[[Property]]
	float chgDmgscal = 2.0f; //��¡ ���ݽ� ��������(������) //��¡�ð��� +=n?
	[[Property]]
	float chgSpd = 3.0f;  //��¡���ݼӵ�  n�ʰ� ��¡ �ٽø��ϴ½ð�?
	[[Property]]
	float chgDelay = 1.0f; //��¡���� �ĵ����� n�ʰ� ���ۺҰ�
	[[Property]]
	float chgRange = 4.0f; //��¡���� ��Ÿ�
	[[Property]]
	float chgHitbox = 4.0f; //��¡���� ����?
	[[Property]]
	float chgKnockback = 1.0f; //��¡���� �˹�Ÿ�
	[[Property]]
	int ChargeAttackBulletCount = 5;
	[[Property]]
	int ChargeAttackBulletAngle = 15;


	//������
	[[Property]]
	int durMax = 10; //�ִ� ������
	[[Property]]
	int durUseAtk = 1; //�Ϲ� ���ݽ� �Ҹ� ������
	[[Property]]
	int durUseChg = 1; //��¡�� �ð��� �Ҹ� ������
	[[Property]]
	int durUseBuf = 100; //�������� �Ҹ� ������(����)
	int curDur = durMax;  //���� ������
	bool isBreak = false;   //���� �μ���Ȯ�ο�
	
	//�ƽý� -> �÷��̾� ���ƿË����°͵�
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //

	
	
};
