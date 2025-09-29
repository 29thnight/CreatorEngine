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

	
	BehaviorTreeComponent* BT = nullptr;
	BlackBoard* BB = nullptr;
	
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
	float BP001Dist = 5.0f; //������ �� �Ÿ� 
	float BP001Widw = 1.0f; //������ �� ��    -> ��� ���̴� �ͺ��� ũ�ų� ������ ����
	[[Property]]
	float BP002RadiusSize = 1.0f; //����ü ������
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

	GameObject* m_target = nullptr;

	//������ Ư���ϰ� 
	GameObject* m_chunsik = nullptr;
	float m_chunsikRadius = 20.f;

	void RotateToTarget();

	//BP0033,0034 �� ���� ���� ���� ���������� ��ü�� �� ����Ǿ������� Ȯ���ϰ� ��ü�� ���� �ɶ� ���� �ൿ�� ���� �Լ�
	bool usePatten = false;
	void PattenActionIdle();

	[[Method]]
	void BP0031();
	[[Method]]
	void BP0032();
	[[Method]]
	void BP0033();
	[[Method]]
	void BP0034();
};
