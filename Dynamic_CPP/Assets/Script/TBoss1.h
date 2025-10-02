#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"
#include "TBoss1.generated.h"

class BehaviorTreeComponent;
class BlackBoard;
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

	

	// 패턴 타입을 지정하여 외부에서 패턴 시작을 명령합니다.
	enum class EPatternType {
		None,
		BP0034
		// 다른 패턴들을 여기에 추가...
	};

	enum class EPatternPhase {
		Inactive,
		Spawning,
		Waiting,
	};

	EPatternType  m_activePattern = EPatternType::None;
	EPatternPhase m_patternPhase = EPatternPhase::Inactive;

	BehaviorTreeComponent* BT = nullptr;
	BlackBoard* BB = nullptr;

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

	//range
	[[Property]]
	float BP001RadiusSize = 1.0f; //투사체 사이즈
	float BP001Speed = 2.0f;
	float BP001Delay = 5.0f;
	[[Property]]
	float BP002Dist = 5.0f; //데미지 들어갈 거리 
	float BP002Widw = 1.0f; //데미지 들어갈 폭    -> 모션 보이는 것보다 크거나 작을수 있음
	[[Property]]
	float BP003RadiusSize = 5.0f; //폭파시 체크 범위 

	//move
	[[Property]]
	float MoveSpeed = 1.0f;
	
	//coolTime 요거 관련해서는 좀 물어보자
	[[Property]]
	float BP003Delay = 1.0f; //생성되고 터지는 딜레이
	

	//발사체 오브젝트들
	std::vector<GameObject*> BP001Objs;
	std::vector<GameObject*> BP003Objs;


	//보스만 특수하게 
	GameObject* m_chunsik = nullptr;
	float m_chunsikRadius = 20.f;

	void RotateToTarget();

	//void StartPattern(EPatternType type); //쓸까 말까 고민중


	//BP0033,0034 용 광역 패턴 사용시 장판패턴이 전체가 다 종료되었는지를 확인하고 전체가 종료 될때 까지 행동을 막는 함수
	bool usePatten = false;
	int pattenIndex = 0;
	std::vector<std::pair<int, Mathf::Vector3>> BP0034Points;
	float bp0034Timer = 0.0f;
	float delay = 1.0f;
	void UpdatePattern(float tick);
	void Update_BP0034(float tick);
	void Calculate_BP0034();
	void EndPattern();


	[[Method]]
	void BP0011();
	[[Method]]
	void BP0012();
	[[Method]]
	void BP0013();
	[[Method]]
	void BP0014();




	[[Method]]
	void BP0031();
	[[Method]]
	void BP0032();
	[[Method]]
	void BP0033();
	[[Method]]
	void BP0034();
};
