#pragma once
#include "Core.Minimal.h"
#include "Core.Definition.h"
#include "KeyState.h"
#include "InputAction.generated.h"
#include "InputManager.h"
enum class ActionType
{
	Value,
	Button,
};
AUTO_REGISTER_ENUM(ActionType);
extern std::string ActionTypeString(ActionType _actionType);

enum class InputValueType
{
	None,
	Float,
	Vector2,
};
AUTO_REGISTER_ENUM(InputValueType);

struct InputValue
{
	float fValue;
	Mathf::Vector2 v2Value;
};


extern std::string InputTypeString(InputType _inputType);
extern std::string KeyStateString(KeyState _keyState);
extern std::string ControllerButtonString(ControllerButton _btn);
extern std::string KeyBoardString(KeyBoard _key);
class InputAction
{
public:
   ReflectInputAction
	[[Serializable]]
	InputAction() = default;

	std::string actionName;

	InputType inputType= InputType::KeyBoard; //keyboard ,mouse ,gamepad
	void SetInputType(InputType _inputType) {inputType = _inputType;}
	
	ActionType actionType = ActionType::Button; //value , button
	void SetActionType(ActionType _actionType)
	{ 
		actionType = _actionType;
		if (actionType == ActionType::Button)
		{
			key.clear();
			key.resize(4);
			if (inputType == InputType::KeyBoard)
			{
				key[0] = static_cast<size_t>(KeyBoard::A);
			}
			else if (inputType == InputType::GamePad)
			{
				key[0] = static_cast<size_t>(ControllerButton::A);
			}
			else if (inputType == InputType::Mouse)
			{
				key[0] = static_cast<size_t>(MouseKey::LEFT);
			}
		}
		else if (actionType == ActionType::Value)
		{
			SetValueType(InputValueType::Vector2);
			key.clear();
			key.resize(4);
			if (inputType == InputType::KeyBoard)
			{
				key[0] = static_cast<size_t>(KeyBoard::A);
				key[1] = static_cast<size_t>(KeyBoard::B);
				key[2] = static_cast<size_t>(KeyBoard::C);
				key[3] = static_cast<size_t>(KeyBoard::D);
			}
			else if (inputType == InputType::GamePad)
			{
				SetControllerButton(ControllerButton::LEFT_Thumbstick);
			}
			else if (inputType == InputType::Mouse) //»ç½Ç»ó ¸ø¾¸ ¤·¤·
			{
				key[0] = static_cast<size_t>(MouseKey::LEFT);
				key[1] = static_cast<size_t>(MouseKey::LEFT);
				key[2] = static_cast<size_t>(MouseKey::LEFT);
				key[3] = static_cast<size_t>(MouseKey::LEFT);
			}
		}
	};
	KeyState  keystate  = KeyState::Down;  //down hold 
	void SetKeyState(KeyState _keyState) { keystate = _keyState; };

	InputValueType valueType = InputValueType::Vector2;   // float ,vector2
	void SetValueType(InputValueType _inputValueType) { valueType = _inputValueType; };

	std::vector<size_t> key = { 0 };
	void SetKey(std::vector<size_t> _key) { key = _key; };
	void SetControllerButton(ControllerButton _btn);
	size_t playerIndex = 0;


	InputValue value;
	std::string objName;
	[[Property]]
	std::string funName;

	ControllerButton m_controllerButton = ControllerButton::None;
	std::function<void()> buttonAction;
	std::function<void(std::any)> valueAction;

};
