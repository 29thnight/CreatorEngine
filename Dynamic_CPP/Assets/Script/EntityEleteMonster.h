#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "EntityEleteMonster.generated.h"


class BehaviorTreeComponent;
class BlackBoard;
class Animator;
class EffectComponent;
struct Feeler;
class EntityEleteMonster : public Entity
{
public:
   ReflectEntityEleteMonster
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityEleteMonster)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}


	BehaviorTreeComponent* enemyBT = nullptr;
	BlackBoard* blackBoard = nullptr;
	Animator* m_animator = nullptr;
	EffectComponent* markEffect = nullptr; //크리티컬 마크 

	std::vector<GameObject*> m_projectiles; // 공격 투사체
	int m_projectileIndex = 0; // 투사체 번호

	GameObject* target = nullptr; //타겟 오브젝트 
	bool isDead = false; //죽음 여부 

	bool isAttack = false; //공격중인지 여부
	bool isAttackAnimation = false; //공격 에니메이션 실행중인지 여부
	//bool isBoxAttack = false; //박스 공격중인지 여부 => 근접 공격 실행 X
	
	//공통 속성
	[[Property]]
	bool isAsisAction = false; //asis 행동중인지 여부
	[[Property]]
	int m_maxHP = 100;
	int m_currHP = m_maxHP;
	[[Property]]
	float m_enemyReward = 10.f; //처치시 플레이어에게 주는 보상
	//이동 및 추적
	[[Property]]
	float m_moveSpeed = 0.5f;
	[[Property]]
	float m_chaseRange = 10.f; //감지 범위
	[[Property]]
	float m_rangeOutDuration = 2.0f; //추적 범위 벗어난 시간

	//근접 공격 방식  => 근접 공격 실행 X
	

	//원거리 공격 방식 - 투사체 공격
	[[Property]]
	int m_rangedAttackDamage = 10; //원거리 공격 데미지
	[[Property]]
	float m_projectileDamegeRadius = 5.0f;//투사체 데미지 범위
	[[Property]]
	float m_projectileSpeed = 0.1f; //투사체 속도
	[[Property]]
	float m_projectileRange = 20.f; //투사체 최대 사거리
	[[Property]]
	float m_projectileArcHeight = 5.0f;//투사체 최대 높이
	[[Property]]
	float m_rangedAttackCoolTime = 2.f; //원거리 공격 쿨타임

	//MonsterMage 특수
	[[Property]]
	float m_retreatRange = 10.0f; //후퇴 감지 범위
	[[Property]]
	float m_retreatCoolTime = 3.0f; //후퇴 행동 쿨타임
	[[Property]]
	float m_retreatDistance = 4.0f; //1회 후퇴거리
	[[Property]]
	float m_avoidanceStrength = 0.03f; //물체 회피시의 힘

	bool m_isRetreat = false; // 후퇴중
	DirectX::SimpleMath::Vector3 m_previousPos= DirectX::SimpleMath::Vector3::Zero;
	float m_retreatTreval = 0.0f; //후퇴하며 이동한 거리

	[[Property]]
	float m_teleportDistance = 5.0f; //텔레포트 시작 거리
	[[Property]]
	float m_teleportCoolTime = 3.0f; //텔레포트 쿨타임

	bool m_isTeleport = false; //텔레포트 실행중
	bool m_posset = false; //이동 완료시

	std::string m_state = "Idle"; //Idle,Chase,Attack,Dead
	std::string m_identity = "MonsterMage";

	DirectX::SimpleMath::Vector3 m_currentVelocity = DirectX::SimpleMath::Vector3::Zero;


	void UpdatePlayer();

	//근접 공격 방식  => 근접 공격 실행 X
	[[Method]]
	void ShootingAttack(); //원거리 공격 방식 - 투사체 발사

	void ChaseTarget(); //타겟 추적

	void StartRetreat();

	void Retreat(float tick); // 플레이어 접근시 후퇴

	DirectX::SimpleMath::Vector3 ObstacleAvoider(
		const std::vector<Feeler>& feelers,
		const DirectX::SimpleMath::Vector3& currentPosition,
		const DirectX::SimpleMath::Quaternion& currentOrientation,
		const DirectX::SimpleMath::Vector3& currentVelocity,
		float characterShapeRadius,
		float characterShapeHalfHeight,
		float avoidanceStrength,
		unsigned int layerMask);

	void Teleport(); // 일정 거리 이내 접근시 순간이동

	void Dead(); //죽음 처리
	[[Method]]
	void DeadEvent();
	bool EndDeadAnimation = false;
	float deadElapsedTime = 0.f;
	float deadDestroyTime = 1.0f;

	void RotateToTarget(); //타겟 바라보기

	void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override;

	//텔레포트
	// 주어진 위치가 텔레포트 가능한지 검사하는 헬퍼 함수
	bool IsValidTeleportLocation(const Mathf::Vector3& candidatePos, std::vector<GameObject*>& outMonstersToPush,bool onlast);

	// 최종 위치로 텔레포트하고, 밀어낼 몬스터들을 밀어내는 헬퍼 함수
	void PushAndTeleportTo(const Mathf::Vector3& finalPos, const std::vector<GameObject*>& monstersToPush);


	////넥백처리 -> 넉백 X
	//float hittimer = 0.f;
	//Mathf::Vector3 hitPos;
	//Mathf::Vector3 hitBaseScale;
	//Mathf::Quaternion hitrot;
	//float m_knockBackVelocity = 1.f;
	//float m_knockBackScaleVelocity = 1.f;
	//float m_MaxknockBackTime = 0.2f;
};
