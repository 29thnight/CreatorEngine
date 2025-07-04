#pragma once
#include "Core.Minimal.h"
#include "InputAction.h"
#include "ActionMap.generated.h"
class ActionMap
{
public:
   ReflectActionMap
	[[Serializable]]
	ActionMap() = default;
	~ActionMap();

	void AddButtonAction(std::string name, size_t _playerindex, InputType _inputType, size_t _key, KeyState _state, std::function<void()> _action);
	void AddButtonAction(std::string name, size_t _playerindex, InputType _inputType, size_t _key, KeyState _state, void (*_action)());

	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(Mathf::Vector2)> _action);
	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(float)> _action);
	void CheckAction();
	void DeleteAction(const std::string& name);
	InputAction* FindAction(const std::string& name);
	[[Property]]
	std::string m_name;
	[[Property]]
	std::vector<InputAction*> m_actions;
private:
	
};


