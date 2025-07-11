#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "EntityAsis.generated.h"

class EntityItem;
class EntityAsis : public Entity
{
public:
   ReflectEntityAsis
		[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EntityAsis)
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
	virtual void OnDisable() override {}
	virtual void OnDestroy() override {}
public:
	virtual void Interact() override {}

	bool AddItem(EntityItem* item);
	void Purification(float tick);

	EntityItem* GetPurificationItemInEntityItemQueue();
private:
	void Inputblabla(Mathf::Vector2 dir);

private:
	std::vector<EntityItem*>		m_EntityItemQueue;

	std::vector<GameObject*>		m_fakeItemQueue;

	int								m_EntityItemQueueIndex = 0;
	int								m_currentEntityItemCount = 0;
	GameObject* asisTail{ nullptr };
	GameObject* asisHead{ nullptr };
	float angle = 0.f;
	float radius = 5.f;
	float timer = 0.f;
	Mathf::Vector2 dir{ 0.f,0.f };

private:
	[[Property]]
	int		maxHP{ 1 };							// �ִ� ü��
	[[Property]]
	float	moveSpeed{ 10.f };					// �̵� �ӵ�

	[[Property]]
	float	graceperiod{ 1.f };					// �ǰ� ���� �ð�
	[[Property]]
	float	staggerDuration{ 1.f };				// �ǰ� ���� �ð�

	[[Property]]
	float	resurrectionMultiple{ 1.f };		// ��Ȱ �ð� ����
	[[Property]]
	float	resurrectionTime{ 1.f };			// ��Ȱ �ð�
	[[Property]]
	float	resurrectionHP{ 10.f };				// ��Ȱ �� ȸ�� ü�·�(%)
	[[Property]]
	float	resurrectionGracePeriod{ 3.f };		// ��Ȱ ���� �ð�

	[[Property]]
	float	tailPurificationDuration{ 1.f };	// ��ȭ ����(���� to ��) �ҿ� �ð� (��)
	[[Property]]
	float	mouthPurificationDuration{ 1.f };	// �Կ��� ��ȭ�� ����Ǵ� �ð� (��)
	[[Property]]
	float	purificationRange{ 10.f };			// �÷��̾ ���� ���� �ڿ� ���� ����
	[[Property]]
	int		maxTailCapacity{ 3 };				// ���� �ֺ����� ���ÿ� ��� ������ �ִ� �ڿ� ��

	[[Property]]
	int		maxPollutionGauge{ 100 };			// ������ ������ �ִ� �� (���� ���� ���� ���� ��)

	[[Property]]
	int		pollutionCoreAmount{ 1 };			// ������ ������ �ִ�ġ ���� �� �����Ǵ� ���� ���� ����

private:
	float	m_currentTailPurificationDuration;
};
