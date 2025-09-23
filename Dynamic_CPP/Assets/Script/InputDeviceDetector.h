#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "InputDeviceDetector.generated.h"

class InputDeviceDetector : public ModuleBehavior
{
public:
   ReflectInputDeviceDetector
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(InputDeviceDetector)
	virtual void Start() override;
	virtual void Update(float tick) override;

	enum class Slot { Left = -1, Neutral = 0, Right = 1 };

public:
	[[Method]]
	void SetSlot(Slot slot);
	[[Method]]
	void MoveSelector(Mathf::Vector2 dir);

private:
	class GameObject* m_gameManagerObj = nullptr;
	class GameManager* m_gameManager = nullptr;
	class PlayerSelector* m_playerSelector = nullptr;

private:
	[[Property]]
	float m_deadZone = 0.4f;     // 입력 데드존
	[[Property]]
	float m_repeatDelay = 0.25f; // 첫 반복까지 대기
	[[Property]]
	float m_repeatRate = 0.10f; // 이후 반복 간격

	Slot  m_slot = Slot::Neutral; // 현재 슬롯
	[[Property]]
	int   m_axisState = 0;        // -1 / 0 / +1 (에지 감지용)
	[[Property]]
	float m_holdTime = 0.f;      // 같은 방향을 얼마나 눌렀는지
	[[Property]]
	float m_repeatTime = 0.f;      // 다음 반복까지 남은 시간
	[[Property]]
	float m_lastDelta = 0.f;    // 마지막 틱의 deltaTime
};
