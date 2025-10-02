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

	

	// ���� Ÿ���� �����Ͽ� �ܺο��� ���� ������ ����մϴ�.
	enum class EPatternType {
		None,
		BP0034
		// �ٸ� ���ϵ��� ���⿡ �߰�...
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

	GameObject* Player1 = nullptr; //��� ����
	GameObject* Player2 = nullptr; 

	GameObject* m_target = nullptr;
	//hp
	[[Property]]
	int m_MaxHp = 1000;
	int m_CurrHp = m_MaxHp;

	//damage --> �̷��� ���Ѱ� �׳� ���ݷ¿� �������� �޾Ƶ� �˴ϴ�. �ϴ� �������� ����
	[[Property]]
	int BP001Damage = 5;
	[[Property]]
	int BP002Damage = 5;
	[[Property]]
	int BP003Damage = 5;

	//range
	[[Property]]
	float BP001RadiusSize = 1.0f; //����ü ������
	float BP001Speed = 2.0f;
	float BP001Delay = 5.0f;
	[[Property]]
	float BP002Dist = 5.0f; //������ �� �Ÿ� 
	float BP002Widw = 1.0f; //������ �� ��    -> ��� ���̴� �ͺ��� ũ�ų� ������ ����
	[[Property]]
	float BP003RadiusSize = 5.0f; //���Ľ� üũ ���� 

	//move
	[[Property]]
	float MoveSpeed = 1.0f;
	
	//coolTime ��� �����ؼ��� �� �����
	[[Property]]
	float BP003Delay = 1.0f; //�����ǰ� ������ ������
	

	//�߻�ü ������Ʈ��
	std::vector<GameObject*> BP001Objs;
	std::vector<GameObject*> BP003Objs;


	//������ Ư���ϰ� 
	GameObject* m_chunsik = nullptr;
	float m_chunsikRadius = 20.f;

	void RotateToTarget();

	//void StartPattern(EPatternType type); //���� ���� �����


	//BP0033,0034 �� ���� ���� ���� ���������� ��ü�� �� ����Ǿ������� Ȯ���ϰ� ��ü�� ���� �ɶ� ���� �ൿ�� ���� �Լ�
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
