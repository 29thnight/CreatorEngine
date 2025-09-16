#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ITriggerCondition.h"
#include <cstdint>

class EventManager;
/*¿¹Á¦: TimerTrigger - raises TickTimer every Update()
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