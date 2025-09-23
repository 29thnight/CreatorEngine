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

public:
	[[Method]]
	void MoveSelector(Mathf::Vector2 dir);

	void SetPlayerIndex(int index) { m_playerIndex = index; }
	int GetPlayerIndex() const { return m_playerIndex; }

private:
	class GameObject* m_gameManagerObj = nullptr;
	class GameManager* m_gameManager = nullptr;
	class PlayerSelector* m_playerSelector = nullptr;

private:
	// �Է�/����Ʈ �Ķ����
	float m_deadZone = 0.4f;
	float m_repeatDelay = 0.25f;
	float m_repeatRate = 0.10f;

	// �÷��̾ ����
	struct AxisState {
		int   axisDiscrete{ 0 };    // -1/0/+1 (������ ���� ��)
		int   prevAxis{ 0 };
		float holdTime{ 0.f };
		float repeatTime{ 0.f };
	};
	AxisState m_p; // 0,1
	[[Property]]
	int m_playerIndex = 0; // 0 or 1
	[[Property]]
	float m_lastDelta = 0.f;    // ������ ƽ�� deltaTime
};
