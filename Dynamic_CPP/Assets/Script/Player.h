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
	void DeleteCurWeapon();  //�������� �پ��� ����
	void FindNearObject(GameObject* gameObject);

	//�÷��̾� �⺻
	[[Property]]
	int playerIndex = 0;
	[[Property]]
	float  moveSpeed= 0.025f;
	[[Property]]
	float  chargingMoveSpeed = 0.0125f; // ��¡�� �̵��ӵ�
	float  baseMoveSpeed = 0.025f;  //�⺻ �̵��ӵ�
	[[Property]]
	float maxHP = 100;
	float curHP = maxHP;
	std::string curStateName = "Idle";
	std::unordered_map<std::string, BitFlag> playerState;
	void ChangeState(std::string _stateName);
	bool CheckState(flag _flag);  //���� ���¿��� ������ �ൿ����
	//playerState m_state = playerState::Idle;
	[[Method]]
	void Move(Mathf::Vector2 dir);
	void CharacterMove(Mathf::Vector2 dir);
	

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
	int Atk = 1;                     //�⺻���ݷ�   // (�⺻���ݷ� + ������ݷ�  ) * ũ��Ƽ�� ���� 
	[[Property]]
	float AttackRange = 2.5f;        //���ݻ�Ÿ�  //������ �����Ÿ�  //���Ÿ��� źȯ �̼� * ����ִ� �ð� // ���� �ִ�Ÿ����� �ʿ�
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

	bool canMeleeCancel = false; //�и����� �ִϸ��̼� ������ ĵ���������� //Ű������ �̺�Ʈ���� �� ������ true�� �ٲ��ְ� true �϶� �����Է½� �������� ��ȯ
	[[Method]]
	void Cancancel();

	float m_chargingTime = 0.f;      //��¡���� �ð�
	bool isCharging = false;
	bool isChargeAttack = false;
	int  chargeCount = 0; //��������ߴ��� ex 0.3�ʴ��ѹ�
	bool isAttacking = false;
	float attackTime = 0.765f;
	float attackElapsedTime = 0.f;
	float nearDistance = FLT_MAX;
	std::unordered_set<Entity*> AttackTarget; //���� ����,���� �ֵ�
	[[Property]]
	float rangeAngle = 150.f;      //���Ÿ� ������ݽ� ���� ��
	[[Property]]
	float rangeDistacne = 5.f;    //���Ÿ� �����Ÿ� �ִ�Ÿ�
	std::unordered_set<Entity*>   inRangeEnemy; //�� ���� ��Ÿ��� ����
	Entity* curTarget = nullptr;
	int countRangeAttack = 0;
	[[Property]]
	int countSpecialBullet = 5;


	bool OnMoveBomb = false;
	void MoveBombThrowPosition(Mathf::Vector2 dir); //��ź �������� Lstick ���κ��� ��ź���������� ����Ű Ȧ�����϶� ����
	Mathf::Vector3 bombThrowPosition = {0,0,0};
	Mathf::Vector3 bombThrowPositionoffset = { 0,0,0 };
	[[Property]]
	float bombMoveSpeed = 0.005f;  //��ź�������� 
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




	//�ǰ�,����
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
	float ResurrectionElapsedTime = 0.f;
	[[Property]]
	float ResurrectionHP = 50.f;   //��Ȱ�� ȸ���ϴ� HP &����
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //��Ȱ�� �����ð�
	bool sucessAttack = false;
	bool CheckResurrectionByOther();
	void Resurrection();
	void OnHit(); //��Ʈ �ִϸ��̼��� �ߵ��ɋ��� �� 
	void Knockback(Mathf::Vector2 KnockbackpowerXY);

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
	EffectComponent* bombIndicator = nullptr; //��ź ��������ġ ������ ����Ʈ



	GameManager* GM = nullptr;
	GameObject* player = nullptr; // ==GetOwner() ��ũ��Ʈ ����
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
	float testHitPowerX = 1.5f;                     //�⺻���ݷ�   // (�⺻���ݷ� + ������ݷ�  ) * ũ��Ƽ�� ���� 
	[[Property]]
	float testHitPowerY = 0.1f;
	[[Method]]
	void TestHit();
};
