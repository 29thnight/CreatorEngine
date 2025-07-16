#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Player.generated.h"

class Animator;
class Player : public ModuleBehavior
{
public:
   ReflectPlayer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Player)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}

	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;


	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override;
	virtual void OnCollisionExit(const Collision& collision) override;

	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override {}
	virtual void OnDestroy() override {}

	void Move(Mathf::Vector2 dir);
	void CatchAndThrow();
	void Catch();
	void Throw();
	void Dash();

	void StartAttack();
	void Charging();
	void Attack();
	void SwapWeaponLeft();
	void SwapWeaponRight();
	void AddWeapon(GameObject* weapon);
	void DeleteCurWeapon();  //�������� �پ��� ����
	void DeleteWeapon(int index);
	void DeleteWeapon(GameObject* weapon);
	void FindNearObject(GameObject* gameObject);


	[[Method]]
	void OnPunch();
	int m_weaponIndex = 0;
	int m_playerIndex = 0;
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;
	[[Property]]
	float ThrowPowerX = 2.0;
	[[Property]]
	float ThrowPowerY = 10.0;
	int m_comboCount = 0;            //���� �޺�Ƚ��
	[[Property]]
	float m_comboTime = 0.5f;        //�޺������ð�
	float m_comboElapsedTime =0.f;   //�޺������ð� üũ
	float m_chargingTime = 0.f;      //��¡���� �ð�
	bool isCharging = false;
	[[Property]]
	float m_dashPower = 1000.0f; // ����̵��Ÿ�
	float dashGracePeriod = 1.f; //��� �����ð�
	[[Property]]
	float dashCooldown = 3.f; //�뽬 ��Ÿ��
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //����뽬 �����ѽð�
	float m_dubbleDashElapsedTime = 0.f;
	[[Property]]
	int   dashAmount = 2;   //�ִ��ð��� Ƚ��
	int   m_curDashCount = 0;   //���� ���Ӵ���� Ƚ��

	[[Property]]
	float KnockbackPowerX = 200.f;
	[[Property]]
	float KnockbackPowerY = 20.f;


	void TestStun();
	void TestKnockBack();
	bool isStun = false;
	float stunTime = 0.f;
	bool isKnockBack = false;
	[[Property]]
	float KnockBackForceY = 200.f;
	[[Property]]
	float KnockBackForce = 20.f; //�����ְ� ������ �� �˹���
	float KnockBackElapsedTime = 0.f;
	float KnockBackTime = 0.f;  //�˹����ӽð� //  �ѰŸ��°����� ��������
	float m_nearDistance = FLT_MAX;
	std::vector<GameObject*> m_weaponInventory;
	GameObject* m_curWeapon = nullptr;
	GameObject* player = nullptr;
	GameObject* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
	GameObject* m_preNearObject = nullptr;
	Animator* m_animator = nullptr;
};
