#pragma once
#include "KeyState.h"
#include "Core.Minimal.h"

enum class ActionType
{
	Value,
	Button,
};

enum class InputValueType
{
	None,
	Float,
	Vector2,
};

struct InputValue
{
	float fValue;
	Mathf::Vector2 v2Value;
};

struct InputAction
{
	std::string actionName;

	InputType inputType; //keyboard ,mouse ,gamepad
	KeyState  keystate;  //press hold 

	ActionType actionType; //value , button
	InputValueType valueType = InputValueType::None;   // float ,vector2


	std::vector<size_t> key;

	size_t playerIndex = 0;


	InputValue value;
	std::string objName;
	std::string funName;
	void InvokeAction()
	{

	}

	std::function<void()> buttonAction;
	std::function<void(std::any)> valueAction;

};



class ActionMap
{
public:
	ActionMap();
	~ActionMap();

	void AddButtonAction(std::string name, size_t _playerindex,InputType _inputType, size_t _key, KeyState _state, std::function<void()> _action)
	{
		InputAction inputAction;
		inputAction.actionName = name;
		inputAction.playerIndex = _playerindex;

		inputAction.actionType = ActionType::Button;
		inputAction.inputType = _inputType;

		inputAction.key.push_back(_key);
		inputAction.keystate = _state;
		inputAction.buttonAction = _action;
		m_actions.push_back(inputAction);
	}

	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(Mathf::Vector2)> _action)
	{
		InputAction inputAction;
		inputAction.actionName = name;
		inputAction.playerIndex = _playerindex;
		inputAction.actionType = ActionType::Value;
		inputAction.inputType = _inputType;
		inputAction.valueType = _inputValueType;
		if (_inputValueType == InputValueType::Float)
		{
			inputAction.key.resize(2);
		}
		else if (_inputValueType == InputValueType::Vector2)
		{
			inputAction.key.resize(4);
		}
		
		for (int index = 0; index < _keys.size(); index++)
		{
			inputAction.key[index] = _keys[index];
		}
		inputAction.keystate = KeyState::Pressed;
		inputAction.valueAction = [action = std::move(_action)](std::any value) {
			if (value.type() == typeid(Mathf::Vector2)) {
				action(std::any_cast<Mathf::Vector2>(value)); 
			}
			};
		m_actions.push_back(inputAction);

	}
	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(float)> _action)
	{
		InputAction inputAction;
		inputAction.actionName = name;
		inputAction.playerIndex = _playerindex;
		inputAction.actionType = ActionType::Value;
		inputAction.inputType = _inputType;
		inputAction.valueType = _inputValueType;
		if (_inputValueType == InputValueType::Float)
		{
			inputAction.key.resize(2);
		}
		else if (_inputValueType == InputValueType::Vector2)
		{
			inputAction.key.resize(4);
		}

		for (int index = 0; index < _keys.size(); index++)
		{
			inputAction.key[index] = _keys[index];
		}
		inputAction.keystate = KeyState::Pressed;
		inputAction.valueAction = [action = std::move(_action)](std::any value) {
			if (value.type() == typeid(float)) {
				action(std::any_cast<float>(value));
			}
			};
		m_actions.push_back(inputAction);

	}
	void CheckAction();
	std::string m_name;
private:
	std::vector<InputAction> m_actions;
};


