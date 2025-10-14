#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "TestMonsterB.generated.h"

class BehaviorTreeComponent;
class BlackBoard;
class Animator;
class EffectComponent;
class CriticalMark;
class TestMonsterB : public Entity
{
public:
   ReflectTestMonsterB
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TestMonsterB)
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
	CriticalMark* m_criticalMark = nullptr;
	std::vector<GameObject*> m_projectiles;
	int m_projectileIndex = 0;

	GameObject* m_asis = nullptr;
	GameObject* m_player1 = nullptr;
	GameObject* m_player2 = nullptr;

	GameObject* target = nullptr;
	bool isDead = false;
	GameObject* deadObj = nullptr;
	bool isAttack = false; //공격중인지 여부
	bool isAttackAnimation = false; //공격 에니메이션 실행중인지 여부
	bool isAttackRoll = false;
	bool isBoxAttack = false; //박스 공격중인지 여부
	bool isMelee = false; //근접공격을 할지 원거리 공격을 할지

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
	float m_moveSpeed = 0.02f;
	[[Property]]
	float m_chaseRange = 10.f; //감지 범위
	[[Property]]
	float m_rangeOutDuration = 2.0f; //추적 범위 벗어난 시간

	//근접 공격 방식
	float m_attackRange = 2.f;
	
	int m_attackDamage = 10;


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

	std::string m_state = "Idle"; //Idle,Chase,Attack,Dead
	std::string m_identity = "MonsterRange";

	void Dead(); //죽음 처리

	void ChaseTarget(float deltatime); //타겟 추적

	void RotateToTarget(); //타겟 바라보기

	//근접 공격 방식 - 박스 공격
	void AttackBoxOn(); //공격 박스 활성화
	
	void AttackBoxOff(); //공격 박스 비활성화

	void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override; //근접 공격시 데미지 전달

	[[Method]]
	void ShootingAttack(); //원거리 공격 방식 - 투사체 발사
	[[Method]]
	void DeadEvent();



	//넥백처리 
	float hittimer = 0.f;
	Mathf::Vector3 hitPos;
	Mathf::Vector3 hitBaseScale;
	Mathf::Quaternion hitrot;
	float m_knockBackVelocity = 1.f;
	float m_knockBackScaleVelocity = 1.f;
	float m_MaxknockBackTime = 0.2f;

private:
	bool EndDeadAnimation = false;
	float deadElapsedTime = 0.f;
	float deadDestroyTime = 1.0f;
};
