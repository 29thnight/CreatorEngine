#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "Player.generated.h"
class Animator;
class Weapon;
class EntityItem;
class Player : public Entity
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

	[[Method]]
	void Move(Mathf::Vector2 dir);
	[[Method]]
	void CatchAndThrow();
	void Catch();
	void Throw();
	void DropCatchItem();
	[[Method]]
	void ThrowEvent();
	[[Method]]
	void Dash();
	[[Method]]
	void StartAttack();
	[[Method]]
	void Charging();
	[[Method]]
	void Attack();
	[[Method]]
	void SwapWeaponLeft();
	[[Method]]
	void SwapWeaponRight();
	void AddWeapon(Weapon* weapon);
	[[Method]]
	void DeleteCurWeapon();  //�������� �پ��� ����
	void DeleteWeapon(int index);
	void DeleteWeapon(Weapon* weapon);
	void FindNearObject(GameObject* gameObject);
	[[Property]]
	int playerIndex = 0;

	int m_weaponIndex = 0;
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;
	[[Property]]
	float ThrowPowerX = 0.0001;
	[[Property]]
	float ThrowPowerY = 0.0001;
	int m_comboCount = 0;            //���� �޺�Ƚ��
	[[Property]]
	float m_comboTime = 0.5f;        //�޺������ð�
	float m_comboElapsedTime =0.f;   //�޺������ð� üũ
	float m_chargingTime = 0.f;      //��¡���� �ð�
	bool isCharging = false;
	[[Property]]
	float m_dashPower = 5.f; // ����̵��Ÿ�
	float dashGracePeriod = 1.f; //��� �����ð�
	bool isDashing = false; //�뽬��
	float m_dashElapsedTime = 0.f;
	[[Property]]
	float m_dashTime = 0.15f;
	[[Property]]
	float dashCooldown = 3.f; //�뽬 ��Ÿ��
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //����뽬 �����ѽð�
	float m_dubbleDashElapsedTime = 0.f;
	[[Property]]
	int   dashAmount = 1;   //�ִ��ð��� Ƚ��
	int   m_curDashCount = 0;   //���� ���Ӵ���� Ƚ��

	[[Property]]
	float AttackPowerX = 500.f;
	[[Property]]
	float AttackPowerY = 20.f;

	[[Property]]
	float detectAngle = 30.f;

	void TestStun();
	void TestKnockBack();
	bool isStun = false;
	float stunTime = 0.f;
	bool isKnockBack = false;
	[[Property]]
	float KnockBackForceY = 0.1f;
	[[Property]]
	float KnockBackForce = 0.05f; //�����ְ� ������ �� �˹���
	float KnockBackElapsedTime = 0.f;
	float KnockBackTime = 0.f;  //�˹����ӽð� //  �ѰŸ��°����� ��������
	float m_nearDistance = FLT_MAX;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;
	GameObject* player = nullptr;
	EntityItem* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
	GameObject* m_preNearObject = nullptr;
	Animator* m_animator = nullptr;

	GameObject* camera = nullptr;
};
