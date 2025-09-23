#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "EntityEleteMonster.generated.h"


class BehaviorTreeComponent;
class BlackBoard;
class Animator;
class EffectComponent;
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
	EffectComponent* markEffect = nullptr; //ũ��Ƽ�� ��ũ 

	std::vector<GameObject*> m_projectiles; // ���� ����ü
	int m_projectileIndex = 0; // ����ü ��ȣ

	GameObject* target = nullptr; //Ÿ�� ������Ʈ 
	bool isDead = false; //���� ���� 

	bool isAttack = false; //���������� ����
	bool isAttackAnimation = false; //���� ���ϸ��̼� ���������� ����
	//bool isBoxAttack = false; //�ڽ� ���������� ���� => ���� ���� ���� X
	
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

	//���� ���� ���  => ���� ���� ���� X
	

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

	//MonsterMage Ư��
	[[Property]]
	float m_retreatDistance = 10.0f; //���� ���� �Ÿ�
	[[Property]]
	float m_teleportDistance = 5.0f; //�ڷ���Ʈ ���� �Ÿ�
	[[Property]]
	float m_teleportCollTime = 3.0f; //�ڷ���Ʈ ��Ÿ��

	bool m_isTeleport = false; //�ڷ���Ʈ ������
	bool m_posset = false; //�̵� �Ϸ��

	std::string m_state = "Idle"; //Idle,Chase,Attack,Dead
	std::string m_identity = "MonsterMage";

	void UpdatePlayer();

	//���� ���� ���  => ���� ���� ���� X
	[[Method]]
	void ShootingAttack(); //���Ÿ� ���� ��� - ����ü �߻�

	void ChaseTarget(); //Ÿ�� ����

	void Retreat(); // �÷��̾� ���ٽ� ����

	void Teleport(); // ���� �Ÿ� �̳� ���ٽ� �����̵�

	void Dead(); //���� ó��

	void RotateToTarget(); //Ÿ�� �ٶ󺸱�

	void SendDamage(Entity* sender, int damage) override;

	//�ڷ���Ʈ
	// �־��� ��ġ�� �ڷ���Ʈ �������� �˻��ϴ� ���� �Լ�
	bool IsValidTeleportLocation(const Mathf::Vector3& candidatePos, std::vector<GameObject*>& outMonstersToPush);

	// ���� ��ġ�� �ڷ���Ʈ�ϰ�, �о ���͵��� �о�� ���� �Լ�
	void PushAndTeleportTo(const Mathf::Vector3& finalPos, const std::vector<GameObject*>& monstersToPush);


	////�ع�ó�� -> �˹� X
	//float hittimer = 0.f;
	//Mathf::Vector3 hitPos;
	//Mathf::Vector3 hitBaseScale;
	//Mathf::Quaternion hitrot;
	//float m_knockBackVelocity = 1.f;
	//float m_knockBackScaleVelocity = 1.f;
	//float m_MaxknockBackTime = 0.2f;
};
