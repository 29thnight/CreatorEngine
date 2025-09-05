#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Weapon.h"
#include "Bomb.generated.h"
class Player;
class Bomb : public Weapon
{
public:
   ReflectBomb
	[[ScriptReflectionField(Inheritance:Weapon)]]
	MODULE_BEHAVIOR_BODY(Bomb)
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

	virtual void Attack(Player* _Owner, AttackContext _attackContext = {})override;
	void ThrowBomb(Player* _owner, Mathf::Vector3 _targetPos);

	[[Property]]
	float throwSpeed = 0.01f;
	[[Property]]
	float throwPowerY = 4.0f; //������ y��
	[[Property]]
	float duration = 3.0f; //���ޱ��� �ɸ��ð�
	float elapsedTime = 0.f;



	Player* m_ownerPlayer = nullptr;

	bool isThrow = false;
	Mathf::Vector3 m_startPos = { 0,0,0 };
	Mathf::Vector3 m_targetPos = { 0,0,0 };
};
