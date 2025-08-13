#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "Player.generated.h"
#include "ItemType.h"
class Animator;
class Weapon;
class EntityItem;
class Socket;
class EffectComponent;
class Entity;
class CharacterControllerComponent;

enum class playerState
{
	Idle,
	Attack,
	Dead,
	Dash,
	Hit,
	Stun, //부활가능한 상태
};
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
	virtual void LateUpdate(float tick) override;
	virtual void OnDisable() override {}
	virtual void OnDestroy() override {}


	virtual void SendDamage(Entity* sender, int damage) override;
	virtual void OnRay() override;

	[[Method]]
	void SwapWeaponLeft();
	[[Method]]
	void SwapWeaponRight();
	void AddWeapon(Weapon* weapon);
	[[Method]]
	void DeleteCurWeapon();  //쓰던무기 다쓰면 쓸꺼
	void FindNearObject(GameObject* gameObject);

	void TestStun();
	[[Method]]
	void TestKnockBack();

	[[Method]]
	void TestEffect();

	//플레이어 기본
	[[Property]]
	int playerIndex = 0;
	[[Property]]
	float  moveSpeed= 0.025f;
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;
	playerState m_state = playerState::Idle;
	[[Method]]
	void Move(Mathf::Vector2 dir);


	//잡기 던지기
	[[Property]]
	float ThrowPowerX = 6.f;
	[[Property]]
	float ThrowPowerY = 7.f;
	[[Property]]
	float DropPowerX = 2.f;
	[[Property]]
	float DropPowerY = 0.f;
	[[Property]]
	float detectAngle = 30.f;

	float m_nearDistance = FLT_MAX;
	EntityItem* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
	GameObject* m_preNearObject = nullptr;
	[[Method]]
	void CatchAndThrow();
	void Catch();
	void Throw();
	void DropCatchItem();
	[[Method]]
	void ThrowEvent();

	//대시
	[[Property]]
	float dashDistacne = 5.f; // 대시이동거리 
	[[Property]]
	float m_dashTime = 0.15f;
	[[Property]]
	float dashCooldown = 1.f; //대쉬 쿨타임
	[[Property]]
	float dashGracePeriod = 1.f; //대시 무적시간
	[[Property]]
	int   dashAmount = 1;   //최대대시가능 횟수
	bool isDashing = false; //대쉬중
	float m_dashElapsedTime = 0.f;
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //더블대쉬 가능한시간
	float m_dubbleDashElapsedTime = 0.f;
	int   m_curDashCount = 0;   //지금 연속대시한 횟수

	[[Method]]
	void Dash();


	//공격
	[[Property]]
	int Atk = 1;                     //기본공격력   // (기본공격력 + 무기공격력  + 버프) * 크리티컬 배율 
	[[Property]]
	float AttackRange = 2.5f;        //공격사거리
	[[Property]]
	float AttackSpeed = 1.0f;      
	int m_comboCount = 0;            //현재 콤보횟수
	[[Property]]
	float comboDuration = 0.5f;        //콤보유지시간
	float m_comboElapsedTime = 0.f;  //콤보유지시간 체크
	[[Property]]
	float atkFwDistacne = 2.0f;      //기본공격시 전진거리
	float m_chargingTime = 0.f;      //차징중인 시간
	bool isCharging = false;
	bool isAttacking = false;
	float attackTime = 0.980f;
	float attackElapsedTime = 0.f;
	std::unordered_set<Entity*> AttackTarget;

	void MeleeAttack();
	[[Method]]
	void StartAttack();
	[[Method]]
	void Charging();
	[[Method]]
	void Attack1();
	


	//피격,죽음
	bool isDead = false;
	bool isStun = false;
	float stunTime = 0.f;
	//bool isKnockBack = false;
	float KnockBackForceY = 0.1f;
	float KnockBackForce = 0.05f; //때린애가 나한테 줄 넉백힘
	//float KnockBackElapsedTime = 0.f;
	//float KnockBackTime = 0.f;  //넉백지속시간 //  총거리는같지만 빨리끝남
	[[Property]]
	float GracePeriod = 1.0f;       //피격시 무적시간
	[[Property]]
	float ResurrectionRange = 5.f;   //부활가능한 트리거 콜라이더 크기 다른플레이어가 이범위안이면 부활  // 콜라이더 or 스크립트에서 범위계산으로 구현예정
	[[Property]]
	float ResurrectionTime = 3.f;   //부활거리안에 머물러야할 시간
	[[Property]]
	float ResurrectionHP = 50.f;   //부활시 회복하는 HP &비율
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //부활시 무적시간
	

	//버프
	BuffType curBuffType = BuffType::None;
	int buffHitCount = 0; //남은 버프공격횟수
	[[Method]]
	void OnBuff();
	void Buff(Weapon* weapon);


	//무기
	[[Property]]
	float SlotChangeCooldown = 2.0f;
	float SlotChangeCooldownElapsedTime = 0.f;
	int m_weaponIndex = 0;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;




	//이펙트 출력관련
	GameObject* dashObj = nullptr;
	EffectComponent* dashEffect = nullptr;



	GameObject* player = nullptr; // ==GetOwner() 스크립트 주인
	Animator* m_animator = nullptr;
	GameObject* aniOwener = nullptr;
	Socket* handSocket;
	CharacterControllerComponent* m_controller;


	GameObject* camera = nullptr;
};
