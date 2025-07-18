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

	InputType inputType; //keyboard ,mouse ,gamepad
	void SetInputType(InputType _inputType) {inputType = _inputType;}
	
	ActionType actionType = ActionType::Button; //value , button
	void SetActionType(ActionType _actionType) { actionType = _actionType; };
	KeyState  keystate;  //press hold 
	void SetKeyState(KeyState _keyState) { keystate = _keyState; };

	InputValueType valueType = InputValueType::None;   // float ,vector2
	void SetValueType(InputValueType _inputValueType) { valueType = _inputValueType; };

	std::vector<size_t> key;
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
