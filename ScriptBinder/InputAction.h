#pragma once
#include "Core.Minimal.h"
#include "KeyState.h"
#include "InputAction.generated.h"
enum class ActionType
{
	Value,
	Button,
};
AUTO_REGISTER_ENUM(ActionType);

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




class InputAction
{
public:
   ReflectInputAction
	[[Serializable]]
	InputAction() = default;

	std::string actionName;

	InputType inputType; //keyboard ,mouse ,gamepad
	KeyState  keystate;  //press hold 

	ActionType actionType; //value , button
	InputValueType valueType = InputValueType::None;   // float ,vector2


	std::vector<size_t> key;

	size_t playerIndex = 0;


	InputValue value;
	std::string objName;
	[[Property]]
	std::string funName;

	std::function<void()> buttonAction;
	std::function<void(std::any)> valueAction;

};
