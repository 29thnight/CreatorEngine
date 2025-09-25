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
class SoundComponent;
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
	[[Method]]
	void SetCurHP(int hp);
	[[Method]]
	void Damage(int damage);

	Core::Delegate<void, Weapon*, int>	m_AddWeaponEvent;
	Core::Delegate<void, Weapon*, int>	m_UpdateDurabilityEvent;
	Core::Delegate<void, Weapon*, int>	m_ChargingWeaponEvent;
	Core::Delegate<void, int>			m_EndChargingEvent; //TODO : 차징이 취소되었을 때 int slotIndex -> UnsafeBroadcast해야함
	Core::Delegate<void, int>			m_SetActiveEvent; 

	[[Method]]
	void SwapWeaponLeft();
	[[Method]]
	void SwapWeaponRight();
	[[Method]]
	void SwapBasicWeapon();
	[[Method]]
	void AddMeleeWeapon();
	bool AddWeapon(Weapon* weapon);
	[[Method]]
	void DeleteCurWeapon();  //쓰던무기 다쓰면 쓸꺼
	void FindNearObject(GameObject* gameObject);

	//플레이어 기본
	[[Property]]
	int playerIndex = 0;
	[[Property]]
	float moveSpeed = 0.025f;
	[[Property]]
	float chargingMoveSpeed = 0.0125f; // 차징중 이동속도  //미사용중
	float baseMoveSpeed = 0.025f;  //기본 이동속도         //chargingMoveSpeed 사용하게되면 필요
	[[Property]]
	int maxHP = 100;
	int curHP = maxHP;
	std::string curStateName = "Idle";
	std::unordered_map<std::string, BitFlag> playerState;           //스테이트별 행동제어용
	void ChangeState(std::string _stateName);
	bool CheckState(flag _flag);  //현재 상태에서 가능한 행동인지
	//playerState m_state = playerState::Idle;
	[[Method]]
	void Move(Mathf::Vector2 dir);
	void CharacterMove(Mathf::Vector2 dir);
	[[Method]]
	void PlaySoundStep();
	//잡기 던지기
	[[Property]]
	float ThrowPowerX = 6.f;      //들고있던물체 던져서 움직일량
	[[Property]]
	float ThrowPowerY = 7.f;
	[[Property]]
	float DropPowerX = 2.f;        //맞거나 공격하거나등 들고있는물체 그냥 떨어뜨릴떄 움직일량
	[[Property]]
	float DropPowerY = 0.f;
	[[Property]]
	float detectAngle = 30.f;      //물체 던질떄 indicator on/off 정할 각도  

	float m_nearDistance = FLT_MAX;
	EntityItem* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
	GameObject* m_preNearObject = nullptr;
	bool onIndicate = false;
	[[Method]]
	void CatchAndThrow();
	void Catch();
	void Throw();
	void DropCatchItem();
	[[Method]]
	void ThrowEvent();
	void UpdateChatchObject();

	//대시
	[[Property]]
	float dashDistacne = 0.05f; // 대시속도 (dashDistacne 속도 애니메이션 재생시간동안 감) 
	[[Property]]
	float m_dashTime = 0.15f; //현재는 애니메이션 재생시간에 맞춰서 정해짐
	[[Property]]
	float dashCooldown = 1.f; //대쉬 쿨타임
	[[Property]]
	float dashGracePeriod = 1.f; //대시 무적시간
	[[Property]]
	int  dashAmount = 1;   //최대대시가능 횟수
	bool isDashing = false; //대쉬중
	float m_dashElapsedTime = 0.f;  //미사용중
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //더블대쉬 가능한시간
	float m_dubbleDashElapsedTime = 0.f;
	int   m_curDashCount = 0;   //지금 연속대시한 횟수

	[[Method]]
	void Dash();
	[[Method]]
	void PlaySoundDash();
	//공격
	[[Property]]
	int Atk = 1;                     //기본공격력   // (기본공격력 + 무기공격력(차징시 차징공격력)  ) * 크리티컬 배율  = 최종데미지
	int m_comboCount = 0;            //현재 콤보횟수
	[[Property]]
	float comboDuration = 0.5f;        //콤보유지시간
	float m_comboElapsedTime = 0.f;  //콤보유지시간 체크
	[[Property]]
	float rangedAutoAimRange = 10.f; //자동조준 거리 
	[[Property]]
	float rangeAngle = 150.f;      //원거리 무기공격시 유도 각
	bool canMeleeCancel = false; //밀리어택 애니메이션 진행중 캔슬가능한지 //키프레임 이벤트에서 각 시점에 true로 바꿔주고 true 일때 공격입력시 다음공격 전환
	[[Method]]
	void Cancancel();
	void CancelChargeAttack();
	bool startAttack = false;
	float m_chargingTime = 0.f;      //차징중인 시간
	bool isCharging = false;
	bool isChargeAttack = false;
	bool isAttacking = false;
	float nearDistance = FLT_MAX;
	std::unordered_set<Entity*> AttackTarget; //내가 떄린,때릴 애들

	std::unordered_set<Entity*>   inRangeEnemy; //내 공격 사거리안 적들
	Entity* curTarget = nullptr;
	int countRangeAttack = 0;
	[[Property]]
	int countSpecialBullet = 5;   //n발마다 스페셜탄 쏠지 

	bool OnMoveBomb = false;
	void MoveBombThrowPosition(Mathf::Vector2 dir); //폭탄 도착지점 Lstick 으로변경 폭탄무기장착중 공격키 홀드중일때 실행
	Mathf::Vector3 bombThrowPosition = {0,0,0};
	Mathf::Vector3 bombThrowPositionoffset = { 0,0,0 };
	[[Property]]
	float bombMoveSpeed = 0.005f;  //폭탄도착지점 조절 스피드
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

	float calculDamge(bool isCharge = false);
	[[Method]]
	void PlaySlashEvent(); //검기 이펙트 + 사운드 출력
	[[Property]]
	float slash1Offset = 2.f;
	[[Property]]
	float slash2Offset = 2.f;
	[[Property]]
	float slashChargeOffset = 3.f;

	[[Method]]
	void PlaySlashEvent2();
	[[Method]]
	void PlaySlashEvent3();

	bool sucessAttack = false;
	[[Property]]
	float MultipleAttackSpeed = 1.0f;        //미사용중  //추가능력치로 공격속도가빨라질경우 사용 

	//피격,죽음
	bool isStun = false;
	float stunTime = 0.f;
	[[Property]]
	float stunRespawnTime = 5.0f;   //스턴시 아시스 옆으로 위치이동까지 걸리는 시간  화면밖에나간후 n초뒤 아시스옆 생성
	float stunRespawnElapsedTime = 0.f;
	bool  OnInvincibility = false; //무적 on off
	[[Property]]
	float HitGracePeriodTime = 1.0f;       //피격시 무적시간
	float GracePeriodElpasedTime = 0.f;
	float curGracePeriodTime = 0.f;
	[[Property]]
	float ResurrectionRange = 5.f;   //부활가능한 트리거 콜라이더 크기 다른플레이어가 이범위안이면 부활  
	[[Property]]
	float ResurrectionTime = 3.f;   //부활거리안에 머물러야할 시간
	float ResurrectionElapsedTime = 0.f;
	[[Property]]
	float ResurrectionHP = 50.f;   //부활시 회복하는 HP &비율
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //부활시 무적시간
	bool CheckResurrectionByOther();
	void Resurrection();
	bool IsInvincibility() { return OnInvincibility; }
	bool sucessResurrection = false;  
	
	void SetInvincibility(float _GracePeriodTime); //무적설정 무적시간전달
	void EndInvincibility(); //무적 강제종료
	void OnHit(); //히트 애니메이션이 발동될떄만 씀 
	void Knockback(Mathf::Vector2 _KnockbackForce);

	//무기
	[[Property]]
	float SlotChangeCooldown = 1.0f;
	float SlotChangeCooldownElapsedTime = 0.f;
	bool  canChangeSlot = true;

	int m_weaponIndex = 0;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;

	//이펙트 출력관련
	GameObject* dashObj = nullptr;
	EffectComponent* dashEffect = nullptr;
	//EffectComponent* bombIndicator = nullptr; //폭탄 떨어질위치 보여줄 이펙트

	GameManager* GM = nullptr;
	GameObject* player = nullptr; // ==GetOwner() 스크립트 주인
	Animator* m_animator = nullptr;
	GameObject* aniOwner = nullptr;
	Socket* handSocket = nullptr;
	CharacterControllerComponent* m_controller = nullptr;

	GameObject* shootPosObj = nullptr;
	GameObject* Indicator = nullptr;
	GameObject* camera = nullptr;

	GameObject* BombIndicator = nullptr;

	SoundComponent* m_ActionSound; //칼 휘두름, 탄 발사, 잡기,던지기,죽음,부활 등 행동사운드
	SoundComponent* m_MoveSound;   //대시, 걷기 등 이동중사운드
	//이펙트 사운드는 따로 몬스터이펙트등 모아서 관리 or 플레이어껀 따로

	
	bool    onBombIndicate = false;   //테스트용 폭탄인디케이터 추후 UI나 이펙트 변경

	[[Method]]
	void TestKillPlayer();

	[[Property]]
	float testHitPowerX = 1.5f;                     //기본공격력   // (기본공격력 + 무기공격력  ) * 크리티컬 배율 
	[[Property]]
	float testHitPowerY = 0.1f;
	[[Method]]
	void TestHit();
};
