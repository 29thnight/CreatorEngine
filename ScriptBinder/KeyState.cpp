#include "KeyState.h"
#include "InputManager.h"
void KeyboardState::Update()
{

	for (unsigned int i = 0; i < KEYBOARD_COUNT; ++i)
	{
		switch (keyboardkeyStates[i])
		{
		case KeyState::Down:
			keyboardkeyStates[i] = InputManagement->curkeyStates[i] ? KeyState::Pressed : KeyState::Released;
			break;
		case KeyState::Pressed:
			keyboardkeyStates[i] = InputManagement->curkeyStates[i] ? KeyState::Pressed : KeyState::Released;
			break;
		case KeyState::Released:
			keyboardkeyStates[i] = InputManagement->curkeyStates[i] ? KeyState::Down : KeyState::Idle;
			break;
		case KeyState::Idle:
			keyboardkeyStates[i] = InputManagement->curkeyStates[i] ? KeyState::Down : KeyState::Idle;
			break;
		}
	}
}

void MouseState::Update()
{

	for (unsigned int i = 0; i < mouseCount; ++i)
	{
		switch (mousekeyStates[i])
		{
		case KeyState::Down:
			mousekeyStates[i] = InputManagement->curmouseState[i] ? KeyState::Pressed : KeyState::Released;
			break;
		case KeyState::Pressed:
			mousekeyStates[i] = InputManagement->curmouseState[i] ? KeyState::Pressed : KeyState::Released;
			break;
		case KeyState::Released:
			mousekeyStates[i] = InputManagement->curmouseState[i] ? KeyState::Down : KeyState::Idle;
			break;
		case KeyState::Idle:
			mousekeyStates[i] = InputManagement->curmouseState[i] ? KeyState::Down : KeyState::Idle;
			break;
		}
	}

}



void PadState::Update()
{
	for (int padnum = 0; padnum < MAX_CONTROLLER; ++padnum)
	{
		/*if (false == padConnected[padnum])
			continue;*/
		for (unsigned int i = 0; i < padKeyCount; ++i)
		{
			switch (padkeyStates[padnum][i])
			{
			case KeyState::Down:
				padkeyStates[padnum][i] = InputManagement->curpadState[padnum][i] ? KeyState::Pressed : KeyState::Released;
				break;
			case KeyState::Pressed:
				padkeyStates[padnum][i] = InputManagement->curpadState[padnum][i] ? KeyState::Pressed : KeyState::Released;
				break;
			case KeyState::Released:
				padkeyStates[padnum][i] = InputManagement->curpadState[padnum][i] ? KeyState::Down : KeyState::Idle;
				break;
			case KeyState::Idle:
				padkeyStates[padnum][i] = InputManagement->curpadState[padnum][i] ? KeyState::Down : KeyState::Idle;
				break;
			}
		}
	}

}
