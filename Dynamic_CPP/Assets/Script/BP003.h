#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class BP003 : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BP003)
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

	Entity* m_ownerEntity = nullptr;

	int m_damage = 0;
	float m_radius = 0.0f;
	//pos�� ������ ������? �ƴ� �������� �� ���� �ٱ�? �ϴ� ������ ��ü������ �� �����ִ°ɷ� 
	//������ �ð��� ���� ���Ͽ� ����? �ƴϸ� �ð��� ���صΰ�? �ϴ� ���Ҽ� �ְ� �ص���
	float m_delay = 0.0f;
	float m_timer = 0.0f;

	bool isInitialize = false;
	bool isAttackStart = false;

	bool ownerDestory = false;

	//ȸ��?? �ڱ� ��ġ�� �������� �ϴ±� �׷� �÷��� �޾Ƽ� ���ư��°� ������
	bool isOrbiting = false;
	float m_orbitAngle = 0.0f;
	float m_orbitDistance = 0.0f;
	bool m_clockWise = true;

	bool m_useOrbiting = false;
	float m_orbitingStartDelay = 1.0f;
	float m_orbitingEndDelay = 1.0f;

	bool m_itemDrop = false;

	//��... ������ġ �������� �������� �ϴ���? ����� ��ġ �������� �ϴ³� �δ�... �� ģ������ �ϴ� ���� ��ġ �������� ����. �׷� ���� �߰� ��� �ɵ�

	//�׷� �����ϴ� ��ƼƼ�� ������,����,�ð� �� ������ �Ƿ���?
	void Initialize(Entity* owner,Mathf::Vector3 pos, int damage, float radius, float delay, bool itemDrop, bool useOrbiting = false,bool clockwise =true);

	void Explosion(); //�����ϸ� �ֺ� �����;
	void ItemDrop(); //������ ���
};
