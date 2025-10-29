#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "TBoss1.generated.h"

class AnimationController;
class Animator;
class BehaviorTreeComponent;
class BlackBoard;
class RigidBodyComponent;
class CriticalMark;
class TBoss1 : public Entity
{
public:
   ReflectTBoss1
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(TBoss1)
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


	struct PatternItemInfo{
		int totalPatternCount;
		int minItemCount;   // 드랍될 아이템의 최소 개수
		int maxItemCount;   // 드랍될 아이템의 최대 개수
	};

	// 패턴 타입을 지정하여 외부에서 패턴 시작을 명령합니다.
	enum class EPatternType {
		None,
		BP0011,
		BP0012,
		BP0013,
		BP0014,
		BP0021,
		BP0022,
		BP0031,
		BP0032,
		BP0033,
		BP0034
		// 다른 패턴들을 여기에 추가...
	};

	enum class EPatternPhase {
		Inactive, // 비활성
		Starting,  // 시작 모션
		Warning,  // 공격 전조, 경고 시간
		Spawning, // 공격 위치/방향 계산 확정
		Move,          // 이동 단계 --> 패턴 중간에 이동 해야 하는 경우
		Action,   // 실제 애니메이션 재생 및 공격 발생
		WaitForObjects, // 생성된 오브젝트들이 모두 사라질 때까지 대기 (ex : 장판 패턴)
		ComboInterval, // 콤보 공격 사이의 중간 대기
		Waiting,  // 공격 후 딜레이
	};

	//보스 이동 상태 --> 특별히 이동 상태가 필요한 이유는 땅파고 이동하는 모션이 있어서
	enum class EBossMoveState {
		Idle, //대기 이동 가능
		Burrowing, //땅속으로 들어가는중
		Burrowed, //땅속에서 이동중
		Warning, //튀어나오기 전 경고
		Protruding //땅속에서 나오는중
	};

	std::string m_state = "Idle";
	std::string m_identity = "Boss1";


	EPatternType  m_activePattern = EPatternType::None;
	EPatternType m_lastCompletedPattern = EPatternType::None;
	EPatternPhase m_patternPhase = EPatternPhase::Inactive;
	EBossMoveState m_moveState = EBossMoveState::Idle;

	EPatternType GetActivePattern() const { return m_activePattern; }
	EPatternPhase GetPatternPhase() const { return m_patternPhase; }

	EPatternType GetLastCompletedPattern() const { return m_lastCompletedPattern; }
	void ConsumeLastCompletedPattern() { m_lastCompletedPattern = EPatternType::None; }
	float GetAttackWarningTime() { return AttackWarningTime; }

	//디버그용 핼퍼
	std::string GetPatternTypeToString(EPatternType type);
	std::string GetPatternPhaseToString(EPatternPhase phase);


	//패턴 관련 변수들
	bool usePatten = false;
	int pattenIndex = 0;
	float BPTimer = 0.0f;

	[[Property]]
	float AttackWarningTime = 1.0f; //공격 경고 시간
	[[Property]]
	float AttackDelayTime = 0.5f; //패턴 후 딜레이 시간
	[[Property]]
	float ComboIntervalTime = 0.3f; // 콤보 공격 사이 대기 시간


	BehaviorTreeComponent* BT = nullptr;
	BlackBoard* BB = nullptr;
	Animator* m_animator = nullptr;
	AnimationController* m_anicontroller = nullptr;
	RigidBodyComponent* m_rigid = nullptr;

	GameObject* Player1 = nullptr; //들고 있자
	GameObject* Player2 = nullptr; 

	GameObject* m_target = nullptr;
	//hp
	[[Property]]
	int m_MaxHp = 1000;
	int m_CurrHp = m_MaxHp;

	//damage --> 이렇게 안한고 그냥 공격력에 연산으로 받아도 됩니다. 일단 귀찮으니 다줌
	[[Property]]
	int BP001Damage = 5;
	[[Property]]
	int BP002Damage = 5;
	[[Property]]
	int BP003Damage = 5;
	[[Property]]
	int ProtrudeDamage = 5;

	//range
	[[Property]]
	float BP001RadiusSize = 3.0f; //투사체 사이즈
	[[Property]]
	float BP001Speed = 3.0f;
	[[Property]]
	float BP001Delay = 5.0f;
	[[Property]]
	float BP002Dist = 5.0f; //데미지 들어갈 거리 
	[[Property]]
	float BP002Widw = 1.0f; //데미지 들어갈 폭    -> 모션 보이는 것보다 크거나 작을수 있음
	[[Property]]
	float BP003RadiusSize = 5.0f; //폭파시 체크 범위 
	[[Property]]
	float ProtrudeRadiusSize = 3.0f;

	//move
	[[Property]]
	float MoveSpeed = 1.0f;
	
	//coolTime 요거 관련해서는 좀 물어보자
	[[Property]]
	float BP003Delay = 1.0f; //생성되고 터지는 딜레이

	//보스공격히트시  플레이어넉백강도, 일단 패턴모두 동일
	[[Property]]
	float KnockbackDistacneX = 0.04f;
	[[Property]]
	float KnockbackDistacneY = 0.01f;




	[[Property]]
	int BP0031_MIN_ITEM = 2;
	[[Property]]
	int BP0031_MAX_ITEM = 2;

	[[Property]]
	int BP0032_MIN_ITEM = 2;
	[[Property]]
	int BP0032_MAX_ITEM = 3;

	[[Property]]
	int BP0033_MIN_ITEM = 2;
	[[Property]]
	int BP0033_MAX_ITEM = 3;

	[[Property]]
	int BP0034_MIN_ITEM = 4;
	[[Property]]
	int BP0034_MAX_ITEM = 5;


	

	//발사체 오브젝트들
	std::vector<GameObject*> BP001Objs;
	std::vector<GameObject*> BP003Objs;

	//effect
	Prefab* fallDownEff = nullptr;
	GameObject* DownEffobj = nullptr;
	Prefab* raiseUpEff = nullptr;
	GameObject* UpEffobj = nullptr;
	Prefab* meleeAtkEff = nullptr;
	GameObject* AtkEffobj = nullptr;
	Prefab* meleeIndicator = nullptr;
	GameObject* Indicatorobj = nullptr;
	Prefab* protrudeIndecator = nullptr;
	GameObject* protrudeIndicatorobj = nullptr;
	


	GameObject* debugMelee = nullptr;
	
	CriticalMark* m_criticalMark = nullptr;

	//보스만 특수하게 
	GameObject* m_chunsik = nullptr;
	float m_chunsikRadius = 10.f;
	Mathf::Vector3 ProtrudePos = Mathf::Vector3::Zero;

	void RotateToTarget();
	void ShootIndexProjectile(int index, Mathf::Vector3 pos,Mathf::Vector3 dir);
	
	void SweepAttackDir(Mathf::Vector3 pos, Mathf::Vector3 dir); //박스 스윕 공격
	void MoveToChunsik(float tick); //춘식이 위치로 이동
	void BurrowMove(float tick); //땅파고 이동
	//void StartPattern(EPatternType type); //쓸까 말까 고민중

	void OnWarningFinished(); //전조 끝났을때 호출
	void OnAttackActionFinished(); //공격 액션 끝났을때 호출
	void OnMoveFinished(); // 이동 애니메이션 종료 시 호출

	//BP0033,0034 용 광역 패턴 사용시 장판패턴이 전체가 다 종료되었는지를 확인하고 전체가 종료 될때 까지 행동을 막는 함수
	
	std::vector<std::pair<int, Mathf::Vector3>> BP0034Points;
	float BP0013delay = 1.0f;
	float BP0034delay = 1.0f;

	bool lockAttack = false;


	void UpdatePatternPhase(float tick);
	void UpdatePatternAction(float tick);


	//void UpdatePattern(float tick);
	void Update_BP0011(float tick);
	void Update_BP0013(float tick);
	void Update_BP0021(float tick);
	void Update_BP0022(float tick);
	void Update_BP0031(float tick);
	void Update_BP0032(float tick);
	void Update_BP0033(float tick);
	void Update_BP0034(float tick);
	void Calculate_BP0034();
	void EndPattern();


	bool isMoved = false;
	bool isAttacked = false;
	bool isBurrow = false;
	float burrowTimer = 0.f;
	[[Property]]
	float burrowDelay = 2.0f;
	[[Property]]
	float burrowSetIndicator = 1.0f;

	float hazardTimer = 0.f;
	float hazardInterval = 8.f; //바닥 패턴 주기
	int actionCount = 0; // 행동 횟수 일정 이상시 강제 대기 상태 


	int p1Count = 0;
	int p2Count = 0;
	void SelectTarget();

	void StartNextComboAttack(); // 콤보 공격용 헬퍼

	int projectileIndex = 0;
	Mathf::Vector3 projectilePos;
	Mathf::Vector3 projectileDir;

	std::map<EPatternType, PatternItemInfo> m_patternItemData;
	std::vector<bool> m_patternItemFlags;
	void SetupPatternItemData(int players);
	void PrepareItemDropsForPattern(EPatternType patternType);

	[[Method]]
	void Burrow(); //땅파고 들어감
	[[Method]]
	void SetBurrow(); //땅파고 들어감
	[[Method]]
	void Protrude(); //타겟 위치로 튀어나옴
	[[Method]]
	void ProtrudeEnd(); //타겟 위치로 튀어나옴
	[[Method]]
	void ProtrudeChunsik(); //춘식이 위치로 튀어나옴
	[[Method]]
	void ProtrudeDamege(); //튀어나오면서 데미지

	void calculateProtrudePoint(GameObject* target);

	[[Method]]
	void CalculToChunsik();
	[[Method]]
	void CalculToTarget();


	[[Method]]
	void ShootProjectile(); //애니메이션 이벤트에서 호출
	[[Method]]
	void SweepAttack(); //애니메이션 이벤트에서 호출
	[[Method]]
	void ShowMeleeIndicator();
	
	bool isIndicator = false;

	[[Method]]
	void BP0011();
	[[Method]]
	void BP0012();
	[[Method]]
	void BP0013();
	[[Method]]
	void BP0014();

	[[Method]]
	void BP0021();
	[[Method]]
	void BP0022();

	[[Method]]
	void BP0031();
	[[Method]]
	void BP0032();
	[[Method]]
	void BP0033();
	[[Method]]
	void BP0034();

	void SendDamage(Entity* sender, int damage, HitInfo hitInfo = HitInfo{}) override;
	bool isDead = false; //죽음 여부 
	GameObject* deadObj = nullptr; //death 이펙트 들어있는 오브젝트
	void Dead();
	[[Method]]
	void DeadEvent();
	bool EndDeadAnimation = false;

	void BossClear(); //보스죽음 애니메이션끝나면서 연출 끝나는 타이밍(연출에따라 달라짐) 후에 GameManager에 bossClear 신호 주기


};
