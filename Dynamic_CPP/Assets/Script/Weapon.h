#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Weapon.generated.h"

enum class ItemType
{
	Meely,
	Range,
	Explosion,
	Basic,
	None,
};

enum class BuffType
{
	Meely,
	Range,
	Explosion,
	None,
};


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
	void Throw(Player* _player,Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = 0;
	GameObject* ownerPlayer = nullptr; //���ư� ���ã�¤���
	int durabiMax = 5;
	int durabiConsume_Attack = 1;
	int durabiConsume_Charged = 3;
	int durabiConsume_Buff = 5;

	int itemAckDamage = 10;
	float itemAckSpeed = 0.5f;
	float itemAckRange = 1.0f;
	float itemAckKnockbackPower = 0.25f;
	float coop_CriticalDamage = 2.0f;  //�������ݹ��

	float chargedTime = 0.1f;   // 0.n�ʴ� ������1��
	float charged_CriticalDamage = 3.0f; //�������ݹ��
	float chargedSpd = 0.5f;
	float chargedDelay = 0.5f; //�����ĵ�
	float chargedRange = 1.5f;
	float chargedKnockbackPower = 0.5f;

	BuffType buffType = BuffType::None;
	int buffHitCount = 3;  //�������� ����Ƚ��
	float buffSpeed = 0.5f; 
	float buffRange = 1.0f; //�Ʊ����� ����?
	float buffKnockbackPower = 0.15f; //�Ϲݰ��� + �����˹�
	int buffStackMax = 30; 
	
	
};
