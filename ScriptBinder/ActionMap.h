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
struct InputAction
{
	std::string actionName;

	InputType inputType; //keyboard ,mouse ,gamepad
	KeyState  keystate;  //press hold 

	ActionType actionType; //value , button
	InputValueType valueType = InputValueType::None;   // float ,vector2
	size_t key;            
	size_t playerIndex = 0;

	std::string objName;
	std::string funName;
	void InvokeAction()
	{

	}

	std::function<void()> action;
	std::function<void(std::any)> actionValue;
	
};



class ActionMap
{
public:
	ActionMap();
	~ActionMap();


	template <typename T>
	void AddAction(std::string name,size_t _playerindex, ActionType _actionType, InputValueType _valueType, InputType _inputType, T _key, KeyState _state, std::function<void()> _action)
	{
		InputAction inputAction;
		inputAction.actionName = name;
		inputAction.playerIndex = _playerindex;
		inputAction.actionType = _actionType;
		inputAction.valueType = _valueType;
		inputAction.inputType = _inputType;
		inputAction.key = _key;
		inputAction.keystate = _state;
		inputAction.action = _action;
		m_actions.push_back(inputAction);
	}
	template <typename T>
	void AddAction(std::string name, size_t _playerindex, ActionType _actionType,InputType _inputType, T _key, KeyState _state, std::function<void()> _action)
	{
		InputAction inputAction;
		inputAction.actionName = name;
		inputAction.playerIndex = _playerindex;

		inputAction.actionType = _actionType;
		inputAction.inputType = _inputType;
		inputAction.key = static_cast<size_t>(_key);
		inputAction.keystate = _state;
		inputAction.action = _action;
		m_actions.push_back(inputAction);
	}
	void CheckAction();
	std::string m_name;
private:
	std::vector<InputAction> m_actions;
};


