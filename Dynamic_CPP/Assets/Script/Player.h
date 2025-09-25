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
	Core::Delegate<void, int>			m_EndChargingEvent; //TODO : ��¡�� ��ҵǾ��� �� int slotIndex -> UnsafeBroadcast�ؾ���
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
	void DeleteCurWeapon();  //�������� �پ��� ����
	void FindNearObject(GameObject* gameObject);

	//�÷��̾� �⺻
	[[Property]]
	int playerIndex = 0;
	[[Property]]
	float moveSpeed = 0.025f;
	[[Property]]
	float chargingMoveSpeed = 0.0125f; // ��¡�� �̵��ӵ�  //�̻����
	float baseMoveSpeed = 0.025f;  //�⺻ �̵��ӵ�         //chargingMoveSpeed ����ϰԵǸ� �ʿ�
	[[Property]]
	int maxHP = 100;
	int curHP = maxHP;
	std::string curStateName = "Idle";
	std::unordered_map<std::string, BitFlag> playerState;           //������Ʈ�� �ൿ�����
	void ChangeState(std::string _stateName);
	bool CheckState(flag _flag);  //���� ���¿��� ������ �ൿ����
	//playerState m_state = playerState::Idle;
	[[Method]]
	void Move(Mathf::Vector2 dir);
	void CharacterMove(Mathf::Vector2 dir);
	[[Method]]
	void PlaySoundStep();
	//��� ������
	[[Property]]
	float ThrowPowerX = 6.f;      //����ִ���ü ������ �����Ϸ�
	[[Property]]
	float ThrowPowerY = 7.f;
	[[Property]]
	float DropPowerX = 2.f;        //�°ų� �����ϰų��� ����ִ¹�ü �׳� ����߸��� �����Ϸ�
	[[Property]]
	float DropPowerY = 0.f;
	[[Property]]
	float detectAngle = 30.f;      //��ü ������ indicator on/off ���� ����  

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

	//���
	[[Property]]
	float dashDistacne = 0.05f; // ��üӵ� (dashDistacne �ӵ� �ִϸ��̼� ����ð����� ��) 
	[[Property]]
	float m_dashTime = 0.15f; //����� �ִϸ��̼� ����ð��� ���缭 ������
	[[Property]]
	float dashCooldown = 1.f; //�뽬 ��Ÿ��
	[[Property]]
	float dashGracePeriod = 1.f; //��� �����ð�
	[[Property]]
	int  dashAmount = 1;   //�ִ��ð��� Ƚ��
	bool isDashing = false; //�뽬��
	float m_dashElapsedTime = 0.f;  //�̻����
	float m_dashCoolElapsedTime = 0.f; //
	[[Property]]
	float dubbleDashTime = 0.5f; //����뽬 �����ѽð�
	float m_dubbleDashElapsedTime = 0.f;
	int   m_curDashCount = 0;   //���� ���Ӵ���� Ƚ��

	[[Method]]
	void Dash();
	[[Method]]
	void PlaySoundDash();
	//����
	[[Property]]
	int Atk = 1;                     //�⺻���ݷ�   // (�⺻���ݷ� + ������ݷ�(��¡�� ��¡���ݷ�)  ) * ũ��Ƽ�� ����  = ����������
	int m_comboCount = 0;            //���� �޺�Ƚ��
	[[Property]]
	float comboDuration = 0.5f;        //�޺������ð�
	float m_comboElapsedTime = 0.f;  //�޺������ð� üũ
	[[Property]]
	float rangedAutoAimRange = 10.f; //�ڵ����� �Ÿ� 
	[[Property]]
	float rangeAngle = 150.f;      //���Ÿ� ������ݽ� ���� ��
	bool canMeleeCancel = false; //�и����� �ִϸ��̼� ������ ĵ���������� //Ű������ �̺�Ʈ���� �� ������ true�� �ٲ��ְ� true �϶� �����Է½� �������� ��ȯ
	[[Method]]
	void Cancancel();
	void CancelChargeAttack();
	bool startAttack = false;
	float m_chargingTime = 0.f;      //��¡���� �ð�
	bool isCharging = false;
	bool isChargeAttack = false;
	bool isAttacking = false;
	float nearDistance = FLT_MAX;
	std::unordered_set<Entity*> AttackTarget; //���� ����,���� �ֵ�

	std::unordered_set<Entity*>   inRangeEnemy; //�� ���� ��Ÿ��� ����
	Entity* curTarget = nullptr;
	int countRangeAttack = 0;
	[[Property]]
	int countSpecialBullet = 5;   //n�߸��� �����ź ���� 

	bool OnMoveBomb = false;
	void MoveBombThrowPosition(Mathf::Vector2 dir); //��ź �������� Lstick ���κ��� ��ź���������� ����Ű Ȧ�����϶� ����
	Mathf::Vector3 bombThrowPosition = {0,0,0};
	Mathf::Vector3 bombThrowPositionoffset = { 0,0,0 };
	[[Property]]
	float bombMoveSpeed = 0.005f;  //��ź�������� ���� ���ǵ�
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
	void PlaySlashEvent(); //�˱� ����Ʈ + ���� ���
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
	float MultipleAttackSpeed = 1.0f;        //�̻����  //�߰��ɷ�ġ�� ���ݼӵ������������ ��� 

	//�ǰ�,����
	bool isStun = false;
	float stunTime = 0.f;
	[[Property]]
	float stunRespawnTime = 5.0f;   //���Ͻ� �ƽý� ������ ��ġ�̵����� �ɸ��� �ð�  ȭ��ۿ������� n�ʵ� �ƽý��� ����
	float stunRespawnElapsedTime = 0.f;
	bool  OnInvincibility = false; //���� on off
	[[Property]]
	float HitGracePeriodTime = 1.0f;       //�ǰݽ� �����ð�
	float GracePeriodElpasedTime = 0.f;
	float curGracePeriodTime = 0.f;
	[[Property]]
	float ResurrectionRange = 5.f;   //��Ȱ������ Ʈ���� �ݶ��̴� ũ�� �ٸ��÷��̾ �̹������̸� ��Ȱ  
	[[Property]]
	float ResurrectionTime = 3.f;   //��Ȱ�Ÿ��ȿ� �ӹ������� �ð�
	float ResurrectionElapsedTime = 0.f;
	[[Property]]
	float ResurrectionHP = 50.f;   //��Ȱ�� ȸ���ϴ� HP &����
	[[Property]]
	float ResurrectionGracePeriod = 3.0f;  //��Ȱ�� �����ð�
	bool CheckResurrectionByOther();
	void Resurrection();
	bool IsInvincibility() { return OnInvincibility; }
	bool sucessResurrection = false;  
	
	void SetInvincibility(float _GracePeriodTime); //�������� �����ð�����
	void EndInvincibility(); //���� ��������
	void OnHit(); //��Ʈ �ִϸ��̼��� �ߵ��ɋ��� �� 
	void Knockback(Mathf::Vector2 _KnockbackForce);

	//����
	[[Property]]
	float SlotChangeCooldown = 1.0f;
	float SlotChangeCooldownElapsedTime = 0.f;
	bool  canChangeSlot = true;

	int m_weaponIndex = 0;
	std::vector<Weapon*> m_weaponInventory;
	Weapon* m_curWeapon = nullptr;

	//����Ʈ ��°���
	GameObject* dashObj = nullptr;
	EffectComponent* dashEffect = nullptr;
	//EffectComponent* bombIndicator = nullptr; //��ź ��������ġ ������ ����Ʈ

	GameManager* GM = nullptr;
	GameObject* player = nullptr; // ==GetOwner() ��ũ��Ʈ ����
	Animator* m_animator = nullptr;
	GameObject* aniOwner = nullptr;
	Socket* handSocket = nullptr;
	CharacterControllerComponent* m_controller = nullptr;

	GameObject* shootPosObj = nullptr;
	GameObject* Indicator = nullptr;
	GameObject* camera = nullptr;

	GameObject* BombIndicator = nullptr;

	SoundComponent* m_ActionSound; //Į �ֵθ�, ź �߻�, ���,������,����,��Ȱ �� �ൿ����
	SoundComponent* m_MoveSound;   //���, �ȱ� �� �̵��߻���
	//����Ʈ ����� ���� ��������Ʈ�� ��Ƽ� ���� or �÷��̾ ����

	
	bool    onBombIndicate = false;   //�׽�Ʈ�� ��ź�ε������� ���� UI�� ����Ʈ ����

	[[Method]]
	void TestKillPlayer();

	[[Property]]
	float testHitPowerX = 1.5f;                     //�⺻���ݷ�   // (�⺻���ݷ� + ������ݷ�  ) * ũ��Ƽ�� ���� 
	[[Property]]
	float testHitPowerY = 0.1f;
	[[Method]]
	void TestHit();
};
