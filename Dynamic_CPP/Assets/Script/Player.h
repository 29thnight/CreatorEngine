#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "Player.generated.h"
#include "ItemType.h"

class Animator;
class Socket;
class EffectComponent;
class CharacterControllerComponent;


class GameManager;
class Weapon;
class Entity;
class EntityItem;
class EntityEnemy;

enum class playerState 
{
	Idle,
	Attack,
	Dead,
	Dash,
	Hit,
	Stun, //��Ȱ������ ����
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
	virtual void OnRay() override {}

	[[Method]]
	void SwapWeaponLeft();
	[[Method]]
	void SwapWeaponRight();
	void AddWeapon(Weapon* weapon);
	[[Method]]
	void DeleteCurWeapon();  //�������� �پ��� ����
	void FindNearObject(GameObject* gameObject);

	//�÷��̾� �⺻
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


	//��� ������
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

	//���
	[[Property]]
	float dashDistacne = 5.f; // ����̵��Ÿ� 
	[[Property]]
	float m_dashTime = 0.15f;
	[[Property]]
	float dashCooldown = 1.f; //�뽬 ��Ÿ��
	[[Property]]
	float dashGracePeriod = 1.f; //��� �����ð�
	[[Property]]
	int   dashAmount = 1;   //�ִ��ð��� Ƚ��
	bool isDashing = false; //�뽬��
	float m_dashElapsedTime = 0.f;
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //����뽬 �����ѽð�
	float m_dubbleDashElapsedTime = 0.f;
	int   m_curDashCount = 0;   //���� ���Ӵ���� Ƚ��

	[[Method]]
	void Dash();


	//����
	[[Property]]
	int Atk = 1;                     //�⺻���ݷ�   // (�⺻���ݷ� + ������ݷ�  + ����) * ũ��Ƽ�� ���� 
	[[Property]]
	float AttackRange = 2.5f;        //���ݻ�Ÿ�
	[[Property]]
	float AttackSpeed = 1.0f;      
	int m_comboCount = 0;            //���� �޺�Ƚ��
	[[Property]]
	float comboDuration = 0.5f;        //�޺������ð�
	float m_comboElapsedTime = 0.f;  //�޺������ð� üũ
	[[Property]]
	float atkFwDistacne = 2.0f;      //�⺻���ݽ� �����Ÿ�
	[[Property]]
	int  rangedAtkCountMax = 5.0f;   //���Ÿ����� �ִ� ��ŸȽ��
	[[Property]]
	float rangedAtkDelay = 0.3f;     //���Ӱ����� �߻簣 ����
	[[Property]]
	float rangedAtkCooldown = 1.0f;   //���Ӱ��� ������ �߻���ð�
	[[Property]]
	float rangedAutoAimRange = 10.f; //�ڵ����� �Ÿ� 
	[[Property]]
	float minChargedTime = 0.7f; //�ּ� �����ð�


	float m_chargingTime = 0.f;      //��¡���� �ð�
	bool isCharging = false;
	bool isAttacking = false;
	float attackTime = 0.765f;
	float attackElapsedTime = 0.f;
	std::unordered_set<Entity*> AttackTarget; //���� ����,���� �ֵ�
	std::vector<EntityEnemy*>   inRangeEnemy; //�� ���� ��Ÿ��� ����
	EntityEnemy* curTarget = nullptr;
	
	void ChangeAutoTarget(Mathf::Vector2 dir); //����� Lstick ���� Ÿ�ٺ���                 //���ӻ�����϶� ���� 
	void MoveBombThrowPosition(Mathf::Vector2 dir); //��ź �������� Lstick ���κ��� ��ź���������� ����Ű Ȧ�����϶� ����
	Mathf::Vector3 bombThrowPosition = {0,0,0};                                                                        
 	void MeleeAttack();
	void MeleeAttack2(float tick);
	[[Method]]
	void StartAttack();
	[[Method]]
	void Charging();
	[[Method]]
	void Attack1();
	[[Method]]
	void StartRay();
	[[Method]]
	void EndRay();
	void ShootBullet();


	bool startRay = false;

	//�ǰ�,����
	bool isDead = false;
	bool isStun = false;
	float stunTime = 0.f;
	//bool isKnockBack = false;
	float KnockBackForceY = 0.1f;
	float KnockBackForce = 0.05f; //�����ְ� ������ �� �˹���
	[[Property]]
	float GracePeriod = 1.0f;       //�ǰݽ� �����ð�
	[[Property]]
	float ResurrectionRange = 5.f;   //��Ȱ������ Ʈ���� �ݶ��̴� ũ�� �ٸ��÷��̾ �̹������̸� ��Ȱ  // �ݶ��̴� or ��ũ��Ʈ���� ����������� ��������
	[[Property]]
	float ResurrectionTime = 3.f;   //��Ȱ�Ÿ��ȿ� �ӹ������� �ð�
	[[Property]]
	float ResurrectionHP = 50.f;   //��Ȱ�� ȸ���ϴ� HP &����
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //��Ȱ�� �����ð�
	

	//����
	BuffType curBuffType = BuffType::None;
	int buffHitCount = 0; //���� ��������Ƚ��
	[[Method]]
	void OnBuff();
	void Buff(Weapon* weapon);


	//����
	[[Property]]
	float SlotChangeCooldown = 2.0f;
	float SlotChangeCooldownElapsedTime = 0.f;
	bool  canChangeSlot = true;

	int m_weaponIndex = 0;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;




	//����Ʈ ��°���
	GameObject* dashObj = nullptr;
	EffectComponent* dashEffect = nullptr;



	GameManager* GM = nullptr;
	GameObject* player = nullptr; // ==GetOwner() ��ũ��Ʈ ����
	Animator* m_animator = nullptr;
	GameObject* aniOwner = nullptr;
	Socket* handSocket = nullptr;
	CharacterControllerComponent* m_controller = nullptr;


	GameObject* camera = nullptr;
};
