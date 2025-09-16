#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ITriggerCondition.h"
#include <cstdint>

class EventManager;

// Component that owns a condition and binds it to an Event
class EventTrigger : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(EventTrigger)

	void Update(float tick) override
	{
		// 만약 컨디션이 ModuleBehavior라면 엔진이 자동으로 호출할 것이다.
		// 이 래퍼는 편의성 및 /하위 호환성을 위해 남아 있다.
		(void)tick;
	}

	void Init(int id, EventManager* manager, ITriggerCondition* condition)
	{
		m_id = id; m_manager = manager; m_condition = condition;
		if (m_condition && m_manager) m_condition->Bind(m_id, m_manager);
	}

	void SetID(int id) { m_id = id; if (m_condition && m_manager) m_condition->Bind(m_id, m_manager); }
	void SetManager(EventManager* manager) { m_manager = manager; if (m_condition && m_manager) m_condition->Bind(m_id, m_manager); }
	void SetCondition(ITriggerCondition* c) { m_condition = c; if (m_condition && m_manager) m_condition->Bind(m_id, m_manager); }

private:
	int m_id{ 0 };
	EventManager* m_manager{ nullptr };
	//필요한 각 트리거를 작성해야 합니다.(수동) -> 아래 참조
	ITriggerCondition* m_condition{ nullptr };
};

/*예제: TimerTrigger - raises TickTimer every Update()
class TimerTrigger : public ITriggerCondition
{
	public:
	MODULE_BEHAVIOR_BODY(TimerTrigger)
	void Init(float duration) { m_duration = duration; }
	void Update(float dt) override
	{
		m_elapsed += dt;
		EventSignal sig{ EventSignalType::TickTimer };
		sig.f = dt;
		Raise(sig);
		if (m_elapsed >= m_duration) m_finished = true; // optional one-shot
	}
	bool Finished() const { return m_finished; }
private:
	float m_duration{ 0.f };
	float m_elapsed{ 0.f };
	bool m_finished{ false };
};*/