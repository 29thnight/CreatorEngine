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
	EffectComponent* markEffect = nullptr; //ũ��Ƽ�� ��ũ 
	CriticalMark* m_criticalMark = nullptr;
	std::vector<GameObject*> m_projectiles;
	int m_projectileIndex = 0;

	GameObject* m_asis = nullptr;
	GameObject* m_player1 = nullptr;
	GameObject* m_player2 = nullptr;

	GameObject* target = nullptr;
	bool isDead = false;
	GameObject* deadObj = nullptr;
	bool isAttack = false; //���������� ����
	bool isAttackAnimation = false; //���� ���ϸ��̼� ���������� ����
	bool isAttackRoll = false;
	bool isBoxAttack = false; //�ڽ� ���������� ����
	bool isMelee = false; //���������� ���� ���Ÿ� ������ ����

	//���� �Ӽ�
	[[Property]]
	bool isAsisAction = false; //asis �ൿ������ ����
	[[Property]]
	int m_maxHP = 100;
	int m_currHP = m_maxHP;
	[[Property]]
	float m_enemyReward = 10.f; //óġ�� �÷��̾�� �ִ� ����
	//�̵� �� ����
	[[Property]]
	float m_moveSpeed = 0.02f;
	[[Property]]
	float m_chaseRange = 10.f; //���� ����
	[[Property]]
	float m_rangeOutDuration = 2.0f; //���� ���� ��� �ð�

	//���� ���� ���
	float m_attackRange = 2.f;
	
	int m_attackDamage = 10;


	//���Ÿ� ���� ��� - ����ü ����
	[[Property]]
	int m_rangedAttackDamage = 10; //���Ÿ� ���� ������
	[[Property]]
	float m_projectileDamegeRadius = 5.0f;//����ü ������ ����
	[[Property]]
	float m_projectileSpeed = 0.1f; //����ü �ӵ�
	[[Property]]
	float m_projectileRange = 20.f; //����ü �ִ� ��Ÿ�
	[[Property]]
	float m_projectileArcHeight = 5.0f;//����ü �ִ� ����
	[[Property]]
	float m_rangedAttackCoolTime = 2.f; //���Ÿ� ���� ��Ÿ��

	std::string m_state = "Idle"; //Idle,Chase,Attack,Dead
	std::string m_identity = "MonsterRange";

	void Dead(); //���� ó��

	void ChaseTarget(float deltatime); //Ÿ�� ����

	void RotateToTarget(); //Ÿ�� �ٶ󺸱�

	//���� ���� ��� - �ڽ� ����
	void AttackBoxOn(); //���� �ڽ� Ȱ��ȭ
	
	void AttackBoxOff(); //���� �ڽ� ��Ȱ��ȭ

	void SendDamage(Entity* sender, int damage, HitInfo = HitInfo{}) override; //���� ���ݽ� ������ ����

	[[Method]]
	void ShootingAttack(); //���Ÿ� ���� ��� - ����ü �߻�
	[[Method]]
	void DeadEvent();



	//�ع�ó�� 
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
