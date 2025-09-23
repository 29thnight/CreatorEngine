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
	float m_deadZone = 0.4f;     // �Է� ������
	[[Property]]
	float m_repeatDelay = 0.25f; // ù �ݺ����� ���
	[[Property]]
	float m_repeatRate = 0.10f; // ���� �ݺ� ����

	Slot  m_slot = Slot::Neutral; // ���� ����
	[[Property]]
	int   m_axisState = 0;        // -1 / 0 / +1 (���� ������)
	[[Property]]
	float m_holdTime = 0.f;      // ���� ������ �󸶳� ��������
	[[Property]]
	float m_repeatTime = 0.f;      // ���� �ݺ����� ���� �ð�
	[[Property]]
	float m_lastDelta = 0.f;    // ������ ƽ�� deltaTime
};
