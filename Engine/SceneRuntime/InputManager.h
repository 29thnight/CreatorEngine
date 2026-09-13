#pragma once
#include <mathematics/vector2.hpp>
#include <mathematics/vector4.hpp>
#include <GameInput.h>
#include "ClassProperty.h"
#include "KeyState.h"
#include "KeyArray.h"
#include "KeyBitFlag.h"

// GameInput API v3(GAMEINPUT_API_VERSION 3)Î∂ÄÌÑ∞ Î™®Îì† ÌÉÄÏûÖÏù¥
// GameInput::v3 ÎÑ§ÏûÑÏä§ÌéòÏù¥Ïä§ ÏïàÏúºÎ°ú Îì§Ïñ¥Í∞îÎã§(Ïù¥Ï†Ñ Î≤ÑÏ†ÑÏùÄ Ï†ÑÏó≠Ïù¥ÏóàÎã§).
using namespace GameInput::v3;
using namespace Microsoft::WRL;
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
	//≈∞∫∏µÂ ∏∂øÏΩ∫ ***** Down Pressed hold¿Œ∞≈ Down √π∆Ωµµπﬁ∞‘ ºˆ¡§« ø‰ ¿Ã∏ß ≈Î¿œ« ø‰
	void KeyBoardUpdate();
	//¥©∏ß
	bool IsKeyDown(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Down;
	}
	// ¥©∏£¥¬¡ﬂ
	bool IsKeyPressed(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Pressed;
	}
	//∂¿
	bool IsKeyReleased(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Released;
	}

	//≈∞ ºº∆√ ∫Ø∞ÊøÎ?
	bool IsAnyKeyPressed();

	void MouseUpdate();

	void SetMousePos(POINT pos);
	math::vector2 GetMousePos();
	math::vector2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//¥©∏£¥¬¡ﬂ
	bool IsMouseButtonDown(MouseKey button);
	//«—π¯¥©∏•∞≈
	bool IsMouseButtonPressed(MouseKey button);
	//∂¿
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
	//¿Ã æ∆∑°¥¬ ∆–µÂ ƒ¡∆Æ∑—∑Ø
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
	
	math::vector4 vibrations[MAX_CONTROLLER]{};

public:
	ComPtr<IGameInput>				gameInput{};

	KeyboardState					m_keyboardState{};
	IGameInputDevice*				m_keyboardDevice{ nullptr };
	std::vector<GameInputKeyState>	m_GameInputKeyStates{};
	GameInputMouseState				m_GameInputMouseState{};
	//∫Ò∆Æ «√∑π±◊¿« ¡§ºˆ∏¶ ∫∏ø©¡÷¡ˆ
	KeyBitFlag                      m_curKeyStates{};
	MouseBitFlag                    m_curMouseState{};

	math::vector2					m_gameViewPos{};
	math::vector2					m_gameViewSize{};

private:
	HWND							hwnd{};
	//∏∂øÏΩ∫
	MouseState						m_mouseState{};
	math::vector2							m_prevMousePos{};
	math::vector2							m_mousePos{};
	math::vector2							m_mouseDelta{};
	//∏∂øÏΩ∫ »Ÿ
	int16							m_mouseWheelDelta{};
	int16							m_prevMouseWheelDelta{};
	//ƒøº≠ º˚±‚±‚
	bool							m_isCursorHidden{ false };
	bool							m_wantCursorHidden{ false };
	bool							m_gameInputOwned{ true };
	void ApplyCursorHidden(bool hidden);

public:
	GameInputGamepadState			m_GameInputPadState[MAX_CONTROLLER]{};
	GamePadBitFlag					m_curPadState[MAX_CONTROLLER]{};
	//∆–µÂ √÷º⁄∞™
	float							deadZone = 0.24f;
	float							triggerdeadZone = 0.1f;

private:
	IGameInputDevice*				device[4]{};
	PadState						m_padState{};
	math::vector2							m_controllerThumbL[MAX_CONTROLLER]{};
	math::vector2							m_controllerThumbR[MAX_CONTROLLER]{};
	float							m_controllerTriggerL[MAX_CONTROLLER]{}; // øﬁ¬  ∆Æ∏Æ∞≈
	float							m_controllerTriggerR[MAX_CONTROLLER]{}; // ø¿∏•¬  ∆Æ∏Æ∞≈
	float							m_controllerVibrationTime[MAX_CONTROLLER]{};
};

inline static auto InputManagement = InputManager::GetInstance();