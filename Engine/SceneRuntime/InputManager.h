#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../Platform/WindowsSDK/Creator.ComSupport.h"
#else
import winrt_base;
#endif
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#if defined(CE_CONTRACT_STANDALONE)
#include "../Platform/WindowsSDK/Creator.GameInputSDK.h"
#else
import "Creator.GameInputSDK.h";
#endif
#include "../Utility_Framework/Reflection/ClassProperty.h"
#include "KeyState.h"
#include "KeyArray.h"
#include "KeyBitFlag.h"

// GameInput API v3(GAMEINPUT_API_VERSION 3)부터 모든 타입이
// GameInput::v3 네임스페이스 안으로 들어갔다(이전 버전은 전역이었다).
using namespace GameInput::v3;
class InputManager : public Singleton<InputManager>
{
	friend class Singleton<InputManager>;
private:
	InputManager() = default;
	~InputManager() = default;

public:
	bool Initialize(HWND _hwnd);

	void Update(float deltaTime);
public:
	//키보드 마우스 ***** Down Pressed hold인거 Down 첫틱도받게 수정필요 이름 통일필요
	void KeyBoardUpdate();
	//누름
	bool IsKeyDown(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Down;
	}
	// 누르는중
	bool IsKeyPressed(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Pressed;
	}
	//뗌
	bool IsKeyReleased(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Released;
	}

	//키 세팅 변경용?
	bool IsAnyKeyPressed();

	void MouseUpdate();

	void SetMousePos(POINT pos);
	math::vector2 GetMousePos();
	math::vector2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//누르는중
	bool IsMouseButtonDown(MouseKey button);
	//한번누른거
	bool IsMouseButtonPressed(MouseKey button);
	//뗌
	bool IsMouseButtonReleased(MouseKey button);
	void HideCursor();
	void ShowCursor();
	void ResetMouseDelta();
	int16 GetWheelDelta() const;

	// -- Game input ownership gate (PHASE 21 W5, see InputManager.cpp) --
	// Written only by the Editor play-mode controller; the Player never calls it,
	// so the default (owned) keeps shipped input untouched. Only *game* consumers
	// consult it (ActionMap, UIManager, C# Api_Input_*); editor shortcuts and the
	// scene camera keep reading the raw device state.
	void SetGameInputOwned(bool owned);
	bool IsGameInputOwned() const noexcept { return m_gameInputOwned; }
	// The game asked to hide the cursor (intent, independent of ownership).
	bool IsCursorHideRequested() const noexcept { return m_wantCursorHidden; }
	// The OS cursor is actually hidden right now.
	bool IsCursorHidden() const noexcept { return m_isCursorHidden; }
	HWND WindowHandle() const noexcept { return hwnd; }

public:
	//이 아래는 패드 컨트롤러
	void PadUpdate();
	void GamePadUpdate();

	bool IsControllerConnected(DWORD Index);
	bool IsControllerButtonDown(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonPressed(DWORD index, ControllerButton btn) const;
	bool IsControllerButtonReleased(DWORD index, ControllerButton btn) const;

	bool IsControllerTriggerL(DWORD index) const;
	bool IsControllerTriggerR(DWORD index) const;
	math::vector2 GetControllerThumbL(DWORD index) const;
	math::vector2 GetControllerThumbR(DWORD index) const;

	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre, float time);
	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	void UpdateControllerVibration(float tick);
	void SetControllerVibrationTime(DWORD Index, float time);
	
	math::vector4 vibrations[kMaxController]{};

public:
	winrt::com_ptr<IGameInput>				gameInput{};

	KeyboardState					m_keyboardState{};
	IGameInputDevice*				m_keyboardDevice{ nullptr };
	ce::dynamic_array<GameInputKeyState>	m_GameInputKeyStates{};
	GameInputMouseState				m_GameInputMouseState{};
	//비트 플레그의 정수를 보여주지
	key_bit_flag                      m_curKeyStates{};
	mouse_bit_flag                    m_curMouseState{};

	math::vector2					m_gameViewPos{};
	math::vector2					m_gameViewSize{};

private:
	HWND							hwnd{};
	//마우스
	MouseState						m_mouseState{};
	math::vector2							m_prevMousePos{};
	math::vector2							m_mousePos{};
	math::vector2							m_mouseDelta{};
	//마우스 휠
	int16							m_mouseWheelDelta{};
	int16							m_prevMouseWheelDelta{};
	//커서 숨기기
	bool							m_isCursorHidden{ false };
	bool							m_wantCursorHidden{ false };
	bool							m_gameInputOwned{ true };
	void ApplyCursorHidden(bool hidden);

public:
	GameInputGamepadState			m_GameInputPadState[kMaxController]{};
	gamepad_bit_flag					m_curPadState[kMaxController]{};
	//패드 최솟값
	float							deadZone = 0.24f;
	float							triggerdeadZone = 0.1f;

private:
	IGameInputDevice*				device[4]{};
	PadState						m_padState{};
	math::vector2							m_controllerThumbL[kMaxController]{};
	math::vector2							m_controllerThumbR[kMaxController]{};
	float							m_controllerTriggerL[kMaxController]{}; // 왼쪽 트리거
	float							m_controllerTriggerR[kMaxController]{}; // 오른쪽 트리거
	float							m_controllerVibrationTime[kMaxController]{};
};

inline static auto InputManagement = InputManager::GetInstance();