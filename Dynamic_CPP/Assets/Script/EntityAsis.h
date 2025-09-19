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
	int		maxHP{ 1 };							// 최대 체력
	[[Property]]
	float	moveSpeed{ 1.f };					// 이동 속도

	[[Property]]
	float	graceperiod{ 1.f };					// 피격 무적 시간
	[[Property]]
	float	staggerDuration{ 1.f };				// 피격 경직 시간

	[[Property]]
	float	resurrectionMultiple{ 1.f };		// 부활 시간 배율
	[[Property]]
	float	resurrectionTime{ 1.f };			// 부활 시간
	[[Property]]
	float	resurrectionHP{ 10.f };				// 부활 시 회복 체력량(%)
	[[Property]]
	float	resurrectionGracePeriod{ 3.f };		// 부활 무적 시간

	[[Property]]
	float	tailPurificationDuration{ 1.f };	// 정화 연출(꼬리 to 입) 소요 시간 (초)
	[[Property]]
	float	mouthPurificationDuration{ 1.f };	// 입에서 정화가 진행되는 시간 (초)
	[[Property]]
	float	purificationRange{ 10.f };			// 플레이어가 던진 오염 자원 감지 범위
	[[Property]]
	int		maxTailCapacity{ 3 };				// 꼬리 주변에서 동시에 대기 가능한 최대 자원 수

	[[Property]]
	int		maxPollutionGauge{ 100 };			// 오염도 게이지 최대 값 (오염 결정 생성 기준 값)

	[[Property]]
	int		pollutionCoreAmount{ 1 };			// 오염도 게이지 최대치 도달 시 생성되는 오염 결정 개수

private:
	float	m_currentTailPurificationDuration{}; // 꼬리 정화 연출 소요 시간
	float	m_currentStaggerDuration{ 0 };	// 현재 경직 시간
	float	m_currentGracePeriod{ 0 };	// 현재 무적 시간


	// Reward 관련.
private:
	int		m_currentPollutionGauge{ 0 }; // 현재 오염도 게이지
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
	float m_predictNextTime = 2.0f; // 예측 시간
	[[Property]]
	float m_rotateSpeed = 5.f;

#ifdef _DEBUG
	GameObject* DebugPoint{ nullptr };
#endif // _DEBUG
};
