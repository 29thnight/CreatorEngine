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
	std::string itemName = "None";
	[[Property]]
	int itemtype = 0;
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

	



	//�ƽý� -> �÷��̾� ���ƿË����°͵�
	void Throw(Player* _player,Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //���ư� ���ã�¤���




	BuffType buffType = BuffType::None;
	int buffHitCount = 3;  //�������� ����Ƚ��
	float buffSpeed = 0.5f; 
	float buffRange = 1.0f; //�Ʊ����� ����?
	float buffKnockbackPower = 0.15f; //�Ϲݰ��� + �����˹�
	int buffStackMax = 30; 
	
	
};
