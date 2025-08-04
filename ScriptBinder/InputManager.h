#pragma once
#include <GameInput.h>
#include "DLLAcrossSingleton.h"
#include "Core.Mathf.h"
#include "KeyState.h"
#include "KeyArray.h"
#include "KeyBitFlag.h"

using namespace Microsoft::WRL;
class InputManager : public DLLCore::Singleton<InputManager>
{
	friend class DLLCore::Singleton<InputManager>;
private:
	InputManager() = default;
	~InputManager() = default;

public:
	bool Initialize(HWND _hwnd);

	void Update(float deltaTime);
public:
	//Ű���� ���콺 ***** Down Pressed hold�ΰ� Down ùƽ���ް� �����ʿ� �̸� �����ʿ�
	void KeyBoardUpdate();
	//����
	bool IsKeyDown(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Down;
	}
	// ��������
	bool IsKeyPressed(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Pressed;
	}
	//��
	bool IsKeyReleased(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Released;
	}

	//Ű ���� �����?
	bool IsAnyKeyPressed();

	void MouseUpdate();

	void SetMousePos(POINT pos);
	float2 GetMousePos();
	float2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//��������
	bool IsMouseButtonDown(MouseKey button);
	//�ѹ�������
	bool IsMouseButtonPressed(MouseKey button);
	//��
	bool IsMouseButtonReleased(MouseKey button);
	void HideCursor();
	void ShowCursor();
	void ResetMouseDelta();
	int16 GetWheelDelta() const;

public:
	//�� �Ʒ��� �е� ��Ʈ�ѷ�
	void PadUpdate();
	void GamePadUpdate();

	bool IsControllerConnected(DWORD Index);
	bool IsControllerButtonDown(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonPressed(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonReleased(DWORD index, ControllerButton btn) const;

	bool IsControllerTriggerL(DWORD index) const;
	bool IsControllerTriggerR(DWORD index) const;
	Mathf::Vector2 GetControllerThumbL(DWORD index) const;
	Mathf::Vector2 GetControllerThumbR(DWORD index) const;

	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	void UpdateControllerVibration(float tick);
	void SetControllerVibrationTime(DWORD Index, float time);

public:
	ComPtr<IGameInput>				gameInput{};

	KeyboardState					m_keyboardState{};
	IGameInputDevice*				m_keyboardDevice{ nullptr };
	std::vector<GameInputKeyState>	m_GameInputKeyStates{};
	GameInputMouseState				m_GameInputMouseState{};
	//��Ʈ �÷����� ������ ��������
	KeyBitFlag                      m_curKeyStates{};
	MouseBitFlag                    m_curMouseState{};

	Mathf::Vector2					m_gameViewPos{};
	Mathf::Vector2					m_gameViewSize{};

private:
	HWND							hwnd{};
	//���콺
	MouseState						m_mouseState{};
	float2							m_prevMousePos{};
	float2							m_mousePos{};
	float2							m_mouseDelta{};
	//���콺 ��
	int16							m_mouseWheelDelta{};
	int16							m_prevMouseWheelDelta{};
	//Ŀ�� �����
	bool							m_isCursorHidden{ false };

public:
	GameInputGamepadState			m_GameInputPadState[MAX_CONTROLLER]{};
	GamePadBitFlag					m_curPadState[MAX_CONTROLLER]{};
	//�е� �ּڰ�
	float							deadZone = 0.24f;
	float							triggerdeadZone = 0.1f;

private:
	IGameInputDevice*				device[4]{};
	PadState						m_padState{};
	float2							m_controllerThumbL[MAX_CONTROLLER]{};
	float2							m_controllerThumbR[MAX_CONTROLLER]{};
	float							m_controllerTriggerL[MAX_CONTROLLER]{}; // ���� Ʈ����
	float							m_controllerTriggerR[MAX_CONTROLLER]{}; // ������ Ʈ����
	float							m_controllerVibrationTime[MAX_CONTROLLER]{};
};

inline static auto InputManagement = InputManager::GetInstance();