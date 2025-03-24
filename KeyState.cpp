#include "KeyState.h"
#include "GameInputManager.h"
void KeyboardState::Update()
{

	for (unsigned int i = 0; i < KEYBOARD_COUNT; ++i)
	{
		switch (keyboardkeyStates[i])
		{
		case KeyState::Pressed:
			keyboardkeyStates[i] = GInputManagement->curkeyStates[i] ? KeyState::Hold : KeyState::Released;
			break;
		case KeyState::Hold:
			keyboardkeyStates[i] = GInputManagement->curkeyStates[i] ? KeyState::Hold : KeyState::Released;
			break;
		case KeyState::Released:
			keyboardkeyStates[i] = GInputManagement->curkeyStates[i] ? KeyState::Pressed : KeyState::Idle;
			break;
		case KeyState::Idle:
			keyboardkeyStates[i] = GInputManagement->curkeyStates[i] ? KeyState::Pressed : KeyState::Idle;
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
		case KeyState::Pressed:
			mousekeyStates[i] = GInputManagement->curmouseState[i] ? KeyState::Hold : KeyState::Released;
			break;
		case KeyState::Hold:
			mousekeyStates[i] = GInputManagement->curmouseState[i] ? KeyState::Hold : KeyState::Released;
			break;
		case KeyState::Released:
			mousekeyStates[i] = GInputManagement->curmouseState[i] ? KeyState::Pressed : KeyState::Idle;
			break;
		case KeyState::Idle:
			mousekeyStates[i] = GInputManagement->curmouseState[i] ? KeyState::Pressed : KeyState::Idle;
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
			case KeyState::Pressed:
				padkeyStates[padnum][i] = GInputManagement->curpadState[padnum][i] ? KeyState::Hold : KeyState::Released;
				break;
			case KeyState::Hold:
				padkeyStates[padnum][i] = GInputManagement->curpadState[padnum][i] ? KeyState::Hold : KeyState::Released;
				break;
			case KeyState::Released:
				padkeyStates[padnum][i] = GInputManagement->curpadState[padnum][i] ? KeyState::Pressed : KeyState::Idle;
				break;
			case KeyState::Idle:
				padkeyStates[padnum][i] = GInputManagement->curpadState[padnum][i] ? KeyState::Pressed : KeyState::Idle;
				break;
			}
		}
	}

}
