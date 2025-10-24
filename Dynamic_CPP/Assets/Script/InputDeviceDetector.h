#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameInstance.h"
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
	[[Method]]
	void CharSelect();
	[[Method]]
	void ReleaseKey();
	[[Method]]
	void LeaveSelectScene();

	void SetPlayerIndex(int index) { m_playerIndex = index; }
	int GetPlayerIndex() const { return m_playerIndex; }
	bool IsSelectComplete() const { return m_isSelectComplete; }

private:
	class GameObject*		m_gameManagerObj = nullptr;
	class GameManager*		m_gameManager = nullptr;
	class PlayerSelector*	m_playerSelector = nullptr;
	class ImageComponent*	m_controllerImage = nullptr;
	class ImageComponent*	leftPos = nullptr;
	class ImageComponent*	rightPos = nullptr;
	class SelectTimer*		m_selectTimer = nullptr;

private:
	// 입력/리피트 파라미터
	float m_deadZone = 0.4f;
	float m_repeatDelay = 0.25f;
	float m_repeatRate = 0.10f;

	// 플레이어별 상태
	struct AxisState {
		int   axisDiscrete{ 0 };    // -1/0/+1 (데드존 적용 후)
		int   prevAxis{ 0 };
		float holdTime{ 0.f };
		float repeatTime{ 0.f };
	};
	AxisState m_p; // 0,1
	[[Property]]
	int m_playerIndex = 0; // 0 or 1
	[[Property]]
	float m_lastDelta = 0.f;    // 마지막 틱의 deltaTime
	[[Property]]
	float m_requiredSelectHold{ 0.5f };
	[[Property]]
	float m_requiredCancelHold{ 0.5f };
	float m_selectHold{};

	bool m_isSelectComplete{ false };
	bool m_isLeaveSelectScene{ false };
	bool m_isApply{ false };

	CharType charType{};
	PlayerDir dir{};

};
