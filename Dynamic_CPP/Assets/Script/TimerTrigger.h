#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ITriggerCondition.h"
#include <cstdint>

class EventManager;
/*¿¹Á¦: TimerTrigger - raises TickTimer every Update()*/

class TimerTrigger : public ITriggerCondition
{
public:
	MODULE_BEHAVIOR_BODY(TimerTrigger)
	void Update(float dt) override;
	bool Finished() const { return m_finished; }

	void Init(float duration) { m_duration = duration; }
private:
	float m_duration{ 0.f };
	float m_elapsed{ 0.f };
	bool m_finished{ false };
};