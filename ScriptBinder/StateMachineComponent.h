#pragma once
#include "Component.h"
#include "IAIComponent.h"
#include "BlackBoard.h"
#include "StateMachineComponent.generated.h"
#include <vector>
#include <memory>
//
namespace FSM
{
	class FSMState;
	class Transition;
}

class StateMachineComponent :public Component, public IAIComponent
{
public:
	using ConditionFunc = std::function<bool(const BlackBoard&)>;
public:
   ReflectStateMachineComponent
	[[Serializable(Inheritance:Component)]]
   StateMachineComponent() 
   {
	   m_name = "StateMachineComponent"; 
	   m_typeID = TypeTrait::GUIDCreator::GetTypeID<StateMachineComponent>();
   }
   virtual ~StateMachineComponent() = default;

	[[Property]]
	std::string name;

	void Initialize() override;
	//void Tick(float deltaTime) override;
		
	FSM::FSMState* AddState(const std::string& name);
	void RemoveState(FSM::FSMState* state);
	FSM::Transition* AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition);
	void RemoveTransition(FSM::Transition* transition);
	FSM::FSMState* FindStateByName(const std::string& name) const;
private:
	std::vector<std::shared_ptr<FSM::FSMState>> m_states;
	std::vector<std::shared_ptr<FSM::Transition>> m_transitions;
	FSM::FSMState* m_currentState = nullptr;
	BlackBoard m_localBB;
};
