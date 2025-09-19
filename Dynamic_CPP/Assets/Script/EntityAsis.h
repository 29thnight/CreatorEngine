#pragma once
#include "Core.Minimal.h"
#include "CircleQueue.hpp"
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
	virtual void SendDamage(Entity* sender, int damage) override;

	bool AddItem(EntityItem* item);
	void Purification(float tick);
	void PathMove(float tick);
	void Stun();

	// 0~1
	float GetPollutionGaugePercent();

	EntityItem* GetPurificationItemInEntityItemQueue();
	float GetPollutionGaugeRatio() const
	{
		return static_cast<float>(m_currentPollutionGauge) / static_cast<float>(maxPollutionGauge);
	}
private:
	Animator* m_animator = nullptr;
	CircularQueue<EntityItem*>		m_EntityItemQueue;

	int								m_currentEntityItemCount = 0;
	[[Property]]
	GameObject* asisTail{ nullptr };
	[[Property]]
	GameObject* asisHead{ nullptr };
	[[Property]]
	float m_purificationAngle = 0.f;
	[[Property]]
	float m_purificationRadius = 5.f;
	float m_purificationTimer = 0.f;
	Mathf::Vector2 dir{ 0.f,0.f };

private:
	[[Property]]
	int		maxHP{ 1 };							// �ִ� ü��
	[[Property]]
	float	moveSpeed{ 1.f };					// �̵� �ӵ�

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
	float	m_currentTailPurificationDuration{}; // ���� ��ȭ ���� �ҿ� �ð�
	float	m_currentStaggerDuration{ 0 };	// ���� ���� �ð�
	float	m_currentGracePeriod{ 0 };	// ���� ���� �ð�


	// Reward ����.
private:
	int		m_currentPollutionGauge{ 0 }; // ���� ������ ������
public:
	void AddPollutionGauge(int amount);

	// Move (Path)
private:
	GameObject* m_playerObject{ nullptr };
	Mathf::Vector3 nextMovePoint{ 0.f, 0.f, 0.f };

	std::vector<Mathf::Vector3> points;

	int currentPointIndex = 0;

private:
	[[Property]]
	float m_pathRadius = 1.f;
	[[Property]]
	float m_pathEndRadius = 3.f;
	[[Property]]
	float m_predictNextTime = 2.0f; // ���� �ð�
	[[Property]]
	float m_rotateSpeed = 5.f;

#ifdef _DEBUG
	GameObject* DebugPoint{ nullptr };
#endif // _DEBUG
};
