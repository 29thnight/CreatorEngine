#pragma once
#include <GameInput.h>
#include "ClassProperty.h"
#include "Core.Mathf.h"
#include "KeyState.h"
#include "KeyArray.h"
#include "KeyBitFlag.h"

// GameInput API v3(GAMEINPUT_API_VERSION 3)ë¶€í„° ëª¨ë“  íƒ€ì…ì´
// GameInput::v3 ë„¤ì„ìŠ¤í˜ì´ìŠ¤ ì•ˆìœ¼ë¡œ ë“¤ì–´ê°”ë‹¤(ì´ì „ ë²„ì „ì€ ì „ì—­ì´ì—ˆë‹¤).
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
	//Å°º¸µå ¸¶¿ì½º ***** Down Pressed holdÀÎ°Å Down Ã¹Æ½µµ¹Ş°Ô ¼öÁ¤ÇÊ¿ä ÀÌ¸§ ÅëÀÏÇÊ¿ä
	void KeyBoardUpdate();
	//´©¸§
	bool IsKeyDown(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Down;
	}
	// ´©¸£´ÂÁß
	bool IsKeyPressed(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Pressed;
	}
	//¶À
	bool IsKeyReleased(auto key) const
	{
		return m_keyboardState.GetKeyState(static_cast<size_t>(key)) == KeyState::Released;
	}

	//Å° ¼¼ÆÃ º¯°æ¿ë?
	bool IsAnyKeyPressed();

	void MouseUpdate();

	void SetMousePos(POINT pos);
	float2 GetMousePos();
	float2 GetMouseDelta() const;
	bool IsWheelUp();
	bool IsWheelDown();
	//´©¸£´ÂÁß
	bool IsMouseButtonDown(MouseKey button);
	//ÇÑ¹ø´©¸¥°Å
	bool IsMouseButtonPressed(MouseKey button);
	//¶À
	bool IsMouseButtonReleased(MouseKey button);
	void HideCursor();
	void ShowCursor();
	void ResetMouseDelta();
	int16 GetWheelDelta() const;

public:
	//ÀÌ ¾Æ·¡´Â ÆĞµå ÄÁÆ®·Ñ·¯
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

	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre, float time);
	void SetControllerVibration(DWORD Index, float leftMotorSpeed, float rightMotorSpeed, float lowFre, float highFre);
	void UpdateControllerVibration(float tick);
	void SetControllerVibrationTime(DWORD Index, float time);
	
	Mathf::Vector4 vibrations[MAX_CONTROLLER]{};

public:
	ComPtr<IGameInput>				gameInput{};

	KeyboardState					m_keyboardState{};
	IGameInputDevice*				m_keyboardDevice{ nullptr };
	std::vector<GameInputKeyState>	m_GameInputKeyStates{};
	GameInputMouseState				m_GameInputMouseState{};
	//ºñÆ® ÇÃ·¹±×ÀÇ Á¤¼ö¸¦ º¸¿©ÁÖÁö
	KeyBitFlag                      m_curKeyStates{};
	MouseBitFlag                    m_curMouseState{};

	Mathf::Vector2					m_gameViewPos{};
	Mathf::Vector2					m_gameViewSize{};

private:
	HWND							hwnd{};
	//¸¶¿ì½º
	MouseState						m_mouseState{};
	float2							m_prevMousePos{};
	float2							m_mousePos{};
	float2							m_mouseDelta{};
	//¸¶¿ì½º ÈÙ
	int16							m_mouseWheelDelta{};
	int16							m_prevMouseWheelDelta{};
	//Ä¿¼­ ¼û±â±â
	bool							m_isCursorHidden{ false };

public:
	GameInputGamepadState			m_GameInputPadState[MAX_CONTROLLER]{};
	GamePadBitFlag					m_curPadState[MAX_CONTROLLER]{};
	//ÆĞµå ÃÖ¼Ú°ª
	float							deadZone = 0.24f;
	float							triggerdeadZone = 0.1f;

private:
	IGameInputDevice*				device[4]{};
	PadState						m_padState{};
	float2							m_controllerThumbL[MAX_CONTROLLER]{};
	float2							m_controllerThumbR[MAX_CONTROLLER]{};
	float							m_controllerTriggerL[MAX_CONTROLLER]{}; // ¿ŞÂÊ Æ®¸®°Å
	float							m_controllerTriggerR[MAX_CONTROLLER]{}; // ¿À¸¥ÂÊ Æ®¸®°Å
	float							m_controllerVibrationTime[MAX_CONTROLLER]{};
};

inline static auto InputManagement = InputManager::GetInstance();