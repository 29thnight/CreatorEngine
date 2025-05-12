#include "ActionMap.h"
#include "Core.Definition.h"
#include "InputManager.h"
ActionMap::ActionMap()
{
}

ActionMap::~ActionMap()
{
}



void ActionMap::CheckAction()
{

	for (auto& action : m_actions)
	{
		if (action.actionType == ActionType::Button)
		{
			switch (action.inputType)
			{
			case InputType::KeyBoard:
				switch (action.keystate)
				{
				case KeyState::Down:
					if (InputManagement->IsKeyDown(action.key))
						action.action();
					break;
				case KeyState::Pressed:
					if (InputManagement->IsKeyPressed(action.key))
						action.action();
					break;
				case KeyState::Released:
					if (InputManagement->IsKeyReleased(action.key))
						action.action();
					break;
				}
				break;
			case InputType::GamePad:
				switch (action.keystate)
				{
				case KeyState::Down:
					if (InputManagement->IsControllerButtonDown(action.playerIndex,static_cast<ControllerButton>(action.key)))
						action.action();
					break;
				case KeyState::Pressed:
					if (InputManagement->IsControllerButtonPressed(action.playerIndex, static_cast<ControllerButton>(action.key)))
						action.action();
					break;
				case KeyState::Released:
					if (InputManagement->IsControllerButtonReleased(action.playerIndex, static_cast<ControllerButton>(action.key)))
						action.action();
					break;
				}
				break;

			}
		}
		else if (action.actionType == ActionType::Button)
		{
			//abcd
		}
	}




}
