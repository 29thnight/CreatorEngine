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
		if (action->actionType == ActionType::Button)
		{
			switch (action->inputType)
			{
			case InputType::KeyBoard:
				switch (action->keystate)
				{
				case KeyState::Down:
					if (InputManagement->IsKeyDown(action->key[0]))
						action->buttonAction();
					break;
				case KeyState::Pressed:
					if (InputManagement->IsKeyPressed(action->key[0]))
						action->buttonAction();
					break;
				case KeyState::Released:
					if (InputManagement->IsKeyReleased(action->key[0]))
						action->buttonAction();
					break;
				}
				break;
			case InputType::GamePad:
				switch (action->keystate)
				{
				case KeyState::Down:
					if (InputManagement->IsControllerButtonDown(action->playerIndex,static_cast<ControllerButton>(action->key[0])))
						action->buttonAction();
					break;
				case KeyState::Pressed:
					if (InputManagement->IsControllerButtonPressed(action->playerIndex, static_cast<ControllerButton>(action->key[0])))
						action->buttonAction();
					break;
				case KeyState::Released:
					if (InputManagement->IsControllerButtonReleased(action->playerIndex, static_cast<ControllerButton>(action->key[0])))
						action->buttonAction();
					break;
				}
				break;

			}
		}
		else if (action->actionType == ActionType::Value)
		{
			if (action->valueType == InputValueType::Float)
			{

			}
			else if (action->valueType == InputValueType::Vector2)
			{

				switch (action->inputType)
				{
				case InputType::KeyBoard:
					if (InputManagement->IsKeyPressed(action->key[0])) action->value.v2Value.x = -1.0f;
					else if (InputManagement->IsKeyPressed(action->key[1])) action->value.v2Value.x = 1.0f;
					else action->value.v2Value.x = 0.0f;
					if (InputManagement->IsKeyPressed(action->key[2])) action->value.v2Value.y = -1.0f;
					else if (InputManagement->IsKeyPressed(action->key[3])) action->value.v2Value.y = 1.0f;
					else action->value.v2Value.y = 0.0f;
					action->valueAction(action->value.v2Value);
					break;
				case InputType::GamePad:
					if (action->key[0] == static_cast<size_t>(ControllerButton::LEFT_Thumbstick))
						action->value.v2Value = InputManagement->GetControllerThumbL(action->playerIndex);
					else if(action->key[0] == static_cast<size_t>(ControllerButton::RIGHT_Thumbstick))
						action->value.v2Value = InputManagement->GetControllerThumbR(action->playerIndex);

					action->valueAction(action->value.v2Value);
					break; //&&&&& ÄÁÆ®·Ñ·¯ vector2´Â ÁÂ¿ì ½ºÆ½¸¸
				}

			}
			
		}
	}

}

void ActionMap::DeleteAction(const std::string& name)
{	
	auto deleteAction = FindAction(name);
	if (deleteAction != nullptr)
	{
		auto it = std::find(m_actions.begin(), m_actions.end(), deleteAction);
		if (it != m_actions.end())
		{
			delete* it;
			m_actions.erase(it);
		}
	}

}

InputAction* ActionMap::FindAction(const std::string& name)
{
	for (auto& action : m_actions)
	{
		if (action->actionName == name)
		{
			return action;
		}
	}
	return nullptr;
}
