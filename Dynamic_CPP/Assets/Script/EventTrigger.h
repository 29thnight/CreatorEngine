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
		// ���� ������� ModuleBehavior��� ������ �ڵ����� ȣ���� ���̴�.
		// �� ���۴� ���Ǽ� �� /���� ȣȯ���� ���� ���� �ִ�.
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
	//�ʿ��� �� Ʈ���Ÿ� �ۼ��ؾ� �մϴ�.(����) -> �Ʒ� ����
	ITriggerCondition* m_condition{ nullptr };
};

/*����: TimerTrigger - raises TickTimer every Update()
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