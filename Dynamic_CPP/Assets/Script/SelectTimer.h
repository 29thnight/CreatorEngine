#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "SelectTimer.generated.h"

class SelectTimer : public ModuleBehavior
{
public:
   ReflectSelectTimer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(SelectTimer)
	virtual void Start() override;
	virtual void Update(float tick) override;

	bool IsTimerOn() const { return m_isTimerOn; }

private:
	class GameManager* gameManager{};
	class TextComponent* timerText{};

private:
	[[Property]]
	float m_remainTimeSetting{ 5.f };
	float m_remainTimeInternal{};
	bool m_isTimerOn{ false };
};
