#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class EffectComponent;
class MonEleteProjetile : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MonEleteProjetile)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	//�� ģ���� ���ư��� ���� �浹 �˻� �ؾ���
	//OnTriggerEnter,OnCollisionEnter ��� �ϰ� �ݶ��̴� �ٿ�����
	bool isInitialize = false;
	bool m_isMoving = false;

	bool OwnerDestroy = false;
	bool isTrigger = false; //���� �浹 ���Ѵٰ� ����

	Entity* ownerEntity = nullptr;

	Mathf::Vector3 m_Dir = Mathf::Vector3::Zero;
	float m_Speed = 0.0f;
	int m_Damege = 0;

	void Initallize(Entity* owner, int damage, float speed, Mathf::Vector3 dir);
	void Action(GameObject* target);

	//���̳� ȭ�� ������ ������ ������� �ϴ� ��Ȱ��ȭ ���� ����
	void RevertPool();
private:
	EffectComponent* m_effect = nullptr;
};
