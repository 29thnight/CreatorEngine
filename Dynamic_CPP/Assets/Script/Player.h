#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "Player.generated.h"
#include "ItemType.h"
#include "BitFlag.h"
class Animator;
class Socket;
class EffectComponent;
class CharacterControllerComponent;


class GameManager;
class Weapon;
class Entity;
class EntityItem;
class EntityEnemy;




class Player : public Entity
{
public:
   ReflectPlayer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Player)
	virtual void Awake() override {};
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
	virtual void OnRay() override {}
	void Heal(int healAmount);
	Core::Delegate<void, Weapon*, int> m_AddWeaponEvent;
	Core::Delegate<void, Weapon*, int> m_UpdateDurabilityEvent;
	Core::Delegate<void, int> m_SetActiveEvent;

	[[Method]]
	void SwapWeaponLeft();
	[[Method]]
	void SwapWeaponRight();
	[[Method]]
	void SwapBasicWeapon();
	[[Method]]
	void AddMeleeWeapon();
	void AddWeapon(Weapon* weapon);
	[[Method]]
	void DeleteCurWeapon();  //쓰던무기 다쓰면 쓸꺼
	void FindNearObject(GameObject* gameObject);

	//플레이어 기본
	[[Property]]
	int playerIndex = 0;
	[[Property]]
	float  moveSpeed= 0.025f;
	[[Property]]
	float  chargingMoveSpeed = 0.0125f; // 차징중 이동속도
	float  baseMoveSpeed = 0.025f;  //기본 이동속도
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;
	std::string curStateName = "Idle";
	std::unordered_map<std::string, BitFlag> playerState;
	void ChangeState(std::string _stateName);
	bool CheckState(flag _flag);  //현재 상태에서 가능한 행동인지
	//playerState m_state = playerState::Idle;
	[[Method]]
	void Move(Mathf::Vector2 dir);
	void CharacterMove(Mathf::Vector2 dir);
	

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
	bool    onIndicate = false;
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
	int Atk = 1;                     //기본공격력   // (기본공격력 + 무기공격력  ) * 크리티컬 배율 
	[[Property]]
	float AttackRange = 2.5f;        //공격사거리  //근접은 무기사거리  //원거리는 탄환 이속 * 살아있는 시간 // 붐은 최대거리구현 필요
	[[Property]]
	float AttackSpeed = 1.0f;      
	int m_comboCount = 0;            //현재 콤보횟수
	[[Property]]
	float comboDuration = 0.5f;        //콤보유지시간
	float m_comboElapsedTime = 0.f;  //콤보유지시간 체크
	[[Property]]
	float atkFwDistacne = 2.0f;      //기본공격시 전진거리
	[[Property]]
	int  rangedAtkCountMax = 5.0f;   //원거리공격 최대 연타횟수
	[[Property]]
	float rangedAtkDelay = 0.3f;     //연속공격중 발사간 간격
	[[Property]]
	float rangedAtkCooldown = 1.0f;   //연속공격 종료후 발사대기시간
	[[Property]]
	float rangedAutoAimRange = 10.f; //자동조준 거리 
	[[Property]]
	float minChargedTime = 0.7f; //최소 차지시간

	bool canMeleeCancel = false; //밀리어택 애니메이션 진행중 캔슬가능한지 //키프레임 이벤트에서 각 시점에 true로 바꿔주고 true 일때 공격입력시 다음공격 전환
	[[Method]]
	void Cancancel();

	float m_chargingTime = 0.f;      //차징중인 시간
	bool isCharging = false;
	bool isChargeAttack = false;
	int  chargeCount = 0; //차지몇번했는지 ex 0.3초당한번
	bool isAttacking = false;
	float attackTime = 0.765f;
	float attackElapsedTime = 0.f;
	float nearDistance = FLT_MAX;
	std::unordered_set<Entity*> AttackTarget; //내가 떄린,때릴 애들
	[[Property]]
	float rangeAngle = 150.f;      //원거리 무기공격시 유도 각
	[[Property]]
	float rangeDistacne = 5.f;    //원거리 유도거리 최대거리
	std::unordered_set<Entity*>   inRangeEnemy; //내 공격 사거리안 적들
	Entity* curTarget = nullptr;
	int countRangeAttack = 0;
	[[Property]]
	int countSpecialBullet = 5;


	bool OnMoveBomb = false;
	void MoveBombThrowPosition(Mathf::Vector2 dir); //폭탄 도착지점 Lstick 으로변경 폭탄무기장착중 공격키 홀드중일때 실행
	Mathf::Vector3 bombThrowPosition = {0,0,0};
	Mathf::Vector3 bombThrowPositionoffset = { 0,0,0 };
	[[Property]]
	float bombMoveSpeed = 0.005f;  //폭탄도착지점 
 	void MeleeAttack();
	void RangeAttack();
	[[Method]]
	void ShootBullet();
	[[Method]]
	void ShootNormalBullet();
	[[Method]]
	void ShootSpecialBullet();
	void ShootChargeBullet();
	[[Method]]
	void ThrowBomb();

	[[Method]]
	void StartAttack();
	[[Method]]
	void Charging();
	[[Method]]
	void ChargeAttack();
	[[Method]]
	void StartRay();
	[[Method]]
	void EndRay();
	[[Method]]
	void EndAttack();
	bool startRay = false;

	float calculDamge(bool isCharge = false,int _chargeCount = 0);




	//피격,죽음
	bool isStun = false;
	float stunTime = 0.f;
	//bool isKnockBack = false;
	float KnockBackForceY = 0.1f;
	float KnockBackForce = 0.05f; //때린애가 나한테 줄 넉백힘
	[[Property]]
	float GracePeriod = 1.0f;       //피격시 무적시간
	[[Property]]
	float ResurrectionRange = 5.f;   //부활가능한 트리거 콜라이더 크기 다른플레이어가 이범위안이면 부활  // 콜라이더 or 스크립트에서 범위계산으로 구현예정
	[[Property]]
	float ResurrectionTime = 3.f;   //부활거리안에 머물러야할 시간
	float ResurrectionElapsedTime = 0.f;
	[[Property]]
	float ResurrectionHP = 50.f;   //부활시 회복하는 HP &비율
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //부활시 무적시간
	bool sucessAttack = false;
	bool CheckResurrectionByOther();
	void Resurrection();
	void OnHit(); //히트 애니메이션이 발동될떄만 씀 
	void Knockback(Mathf::Vector2 KnockbackpowerXY);

	//무기
	[[Property]]
	float SlotChangeCooldown = 2.0f;
	float SlotChangeCooldownElapsedTime = 0.f;
	bool  canChangeSlot = true;

	int m_weaponIndex = 0;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;




	//이펙트 출력관련
	GameObject* dashObj = nullptr;
	EffectComponent* dashEffect = nullptr;
	EffectComponent* bombIndicator = nullptr; //폭탄 떨어질위치 보여줄 이펙트



	GameManager* GM = nullptr;
	GameObject* player = nullptr; // ==GetOwner() 스크립트 주인
	Animator* m_animator = nullptr;
	GameObject* aniOwner = nullptr;
	Socket* handSocket = nullptr;
	CharacterControllerComponent* m_controller = nullptr;

	GameObject* shootPosObj = nullptr;
	GameObject* Indicator = nullptr;
	GameObject* camera = nullptr;


	[[Method]]
	void TestKillPlayer();

	[[Property]]
	float testHitPowerX = 1.5f;                     //기본공격력   // (기본공격력 + 무기공격력  ) * 크리티컬 배율 
	[[Property]]
	float testHitPowerY = 0.1f;
	[[Method]]
	void TestHit();
};
