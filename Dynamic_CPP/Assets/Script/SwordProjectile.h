#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Player;
class EffectComponent;
class Entity;
class SwordProjectile : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SwordProjectile)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void Initialize(Player* owner, Mathf::Vector3 originpos, Mathf::Vector3 dir, int _damage);

private:
	int m_damage = 0;
	float lifeTime = 5.f; //�ӽÿ�
	Mathf::Vector3 m_moveDir = { 0,0,0 }; //���ư� ����
	Player* m_ownerPlayer = nullptr;
	bool beLateFrame = false; //setposion���� ���� ��ġ�� �������� �ʾ �̷��� ����
	bool OnEffect = false;
	EffectComponent* m_effect = nullptr;
	std::unordered_set<Entity*> targets;
	[[Property]]
	float rangedProjSpd = 0.1f; //�߻�ü �̵��ӵ�
	[[Property]]
	float rangedProjDist = 5.f; //�߻�ü �ִ� �̵��Ÿ� ��? �̵��Ÿ�?
};
