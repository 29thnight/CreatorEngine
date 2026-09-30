#pragma once
#include "Component.h"
#include "IAIComponent.h"
#include "BlackBoard.h"
#include <vector>
#include <memory>
//
namespace FSM
{
	class FSMState;
	class Transition;
}

class [[reflgen::reflect]] StateMachineComponent :public meta::identity<StateMachineComponent, Component>, public IAIComponent
{
   public:
public:
	using ConditionFunc = std::function<bool(const BlackBoard&)>;
public:
   StateMachineComponent() 
   {
   }
   virtual ~StateMachineComponent() = default;

	std::string name;

	void Initialize() override;
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;
	//void Tick(float deltaTime) override;
		
	FSM::FSMState* AddState(const std::string& name);
	void RemoveState(FSM::FSMState* state);
	FSM::Transition* AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition);
	void RemoveTransition(FSM::Transition* transition);
	FSM::FSMState* FindStateByName(const std::string& name) const;
private:
	[[reflgen::ignore]]
	std::vector<std::shared_ptr<FSM::FSMState>> m_states;

	[[reflgen::ignore]]
	std::vector<std::shared_ptr<FSM::Transition>> m_transitions;

	[[reflgen::ignore]]
	FSM::FSMState* m_currentState = nullptr;

	[[reflgen::ignore]]
	BlackBoard m_localBB;
};
