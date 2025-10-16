#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemType.h"
struct HitInfo
{
	Mathf::Vector3 attakerPos{};     //������ worldpos
	Mathf::Vector3 hitPos{};         //�����ָ�����ġ (�ݸ���������ġ ��)
	Mathf::Vector3 hitNormal{};
	Mathf::Vector2 KnockbackForce{}; //�����ָ� �˹��ų�Ÿ� X,Y
	float          KnockbackTime = 0;
	ItemType itemType = ItemType::None;
	BulletType bulletType = BulletType::Normal;
};
class Entity : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(Entity)
	virtual void Awake() override {}
	virtual void Start() override {}
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) {}
	virtual void Update(float tick) override {}
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}
public:
	virtual void Interact() {}
	virtual void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) {} //09-25 sehwan HitInfo �߰�
	virtual void SendKnockBack(Entity* sender, Mathf::Vector2 KnockBackForce) {}
	virtual void OnRay() {};
	virtual void AttakRay() {};
	void HitImpulseStart();
	void HitImpulse();
	virtual void HitImpulseUpdate(float tick);
	void StopHitImpulse();

	int m_currentHP{ 1 };
	int m_maxHP{ 100 };

	float m_maxHitImpulseDuration{ 0.3f };
	float m_currentHitImpulseDuration{ 0.f };
	float m_maxHitImpulseSize{ 1.2f };
	float m_hitImpulseFrequency{ 1.f };

	bool isKnockBack = false;
	float KnockBackElapsedTime = 0.f;
	float KnockBackTime = 0.f;  //�˹����ӽð� //  �ѰŸ��°����� ��������

	Core::Delegate<void> m_onDeathEvent;
	Core::Delegate<void> m_onDamageEvent;
};
