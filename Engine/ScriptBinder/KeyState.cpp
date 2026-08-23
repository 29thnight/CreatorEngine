#include "KeyState.h"
#include "InputManager.h"

constexpr KeyState NextKeyState(KeyState current, bool isDown)
{
	switch (current)
	{
	case KeyState::Idle:
	case KeyState::Released:
		return isDown ? KeyState::Down : KeyState::Idle;

	case KeyState::Down:
	case KeyState::Pressed:
		return isDown ? KeyState::Pressed : KeyState::Released;

	default:
		return KeyState::Idle;
	}
}

void KeyboardState::Update()
{
	for (unsigned int i = 0; i < KEYBOARD_COUNT; ++i)
	{
		bool isDown = InputManagement->m_curKeyStates.Test(i);
		KeyState current = GetKeyState(i);
		SetKeyState(i, NextKeyState(current, isDown));
	}
}

void MouseState::Update()
{
	for (size_t i = 0; i < MOUSE_COUNT; ++i)
	{
		bool isDown = InputManagement->m_curMouseState.Test(i);
		KeyState current = GetKeyState(i);
		SetKeyState(i, NextKeyState(current, isDown));
	}
}

void PadState::Update()
{
	for (int padNum = 0; padNum < MAX_CONTROLLER; ++padNum)
	{
		for (size_t btn = 0; btn < GAMEPAD_KEY_COUNT; ++btn)
		{
			bool isDown = InputManagement->m_curPadState[padNum].Test(btn);
			KeyState current = GetKeyState(padNum, btn);
			SetKeyState(padNum, btn, NextKeyState(current, isDown));
		}
	}
}

