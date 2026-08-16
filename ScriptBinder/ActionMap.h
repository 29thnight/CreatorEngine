#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
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

	InputAction* AddAction();
	void AddButtonAction(std::string name, size_t _playerindex, InputType _inputType, size_t _key, KeyState _state, std::function<void()> _action);
	void AddButtonAction(std::string name, size_t _playerindex, InputType _inputType, size_t _key, KeyState _state, void (*_action)());

	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(Mathf::Vector2)> _action);
	void AddValueAction(std::string name, size_t _playerindex, InputValueType _inputValueType, InputType _inputType, std::vector<size_t> _keys, std::function<void(float)> _action);
	void CheckAction();

	// 이번 프레임에 발동한 액션의 함수 이름을 콜백으로 알린다.
	//
	// 예전에는 여기서 리플렉션으로 C++ 스크립트의 메서드를 직접 불렀다. 호출 주체를
	// 콜백으로 빼면 액션 판정(입력 상태 읽기)과 전달 방식이 분리되어, 관리 스크립트든
	// 네이티브든 같은 판정을 쓸 수 있다.
	void CheckAction(int playerIndex, std::string_view scriptName,
		const std::function<void(const std::string&)>& onFired);
	void DeleteAction(const std::string& name);
	InputAction* FindAction(const std::string& name);
	[[Property]]
	std::string m_name;
	[[Property]]
	std::vector<InputAction*> m_actions;


private:
	
};


