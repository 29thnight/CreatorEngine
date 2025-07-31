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
	void DeleteCurWeapon();  //쓰던무기 다쓰면 쓸꺼
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
	int m_comboCount = 0;            //현재 콤보횟수
	[[Property]]
	float m_comboTime = 0.5f;        //콤보유지시간
	float m_comboElapsedTime =0.f;   //콤보유지시간 체크
	float m_chargingTime = 0.f;      //차징중인 시간
	bool isCharging = false;
	[[Property]]
	float m_dashPower = 5.f; // 대시이동거리
	float dashGracePeriod = 1.f; //대시 무적시간
	bool isDashing = false; //대쉬중
	float m_dashElapsedTime = 0.f;
	[[Property]]
	float m_dashTime = 0.15f;
	[[Property]]
	float dashCooldown = 3.f; //대쉬 쿨타임
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //더블대쉬 가능한시간
	float m_dubbleDashElapsedTime = 0.f;
	[[Property]]
	int   dashAmount = 1;   //최대대시가능 횟수
	int   m_curDashCount = 0;   //지금 연속대시한 횟수

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
	float KnockBackForce = 0.05f; //때린애가 나한테 줄 넉백힘
	float KnockBackElapsedTime = 0.f;
	float KnockBackTime = 0.f;  //넉백지속시간 //  총거리는같지만 빨리끝남
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
