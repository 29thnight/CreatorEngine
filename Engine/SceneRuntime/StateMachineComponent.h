#include "../Containers/ContainerAPI.h"
#pragma once
#include "../Utility_Framework/Foundation/EngineFunc.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "Component.h"
#include "IAIComponent.h"
#include "BlackBoard.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
//
namespace FSM
{
	class FSMState;
	class Transition;
}

class [[reflgen::reflect, reflgen::public_methods]] StateMachineComponent :public meta::identity<StateMachineComponent, Component>, public IAIComponent
{
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
	using ConditionFunc = ce::owned_func<bool(const BlackBoard&)>;
public:
   StateMachineComponent()
   {
   }
   virtual ~StateMachineComponent() = default;

	std::string name;

	void Initialize() override;
	[[reflgen::script_internal]]
	void OnAddedToScene() override;
	[[reflgen::script_internal]]
	void OnRemovingFromScene() override;
	//void Tick(float deltaTime) override;

	FSM::FSMState* AddState(const std::string& name);
	void RemoveState(FSM::FSMState* state);
	FSM::Transition* AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition);
	void RemoveTransition(FSM::Transition* transition);
	FSM::FSMState* FindStateByName(const std::string& name) const;
private:
	[[reflgen::ignore]]
	ce::dynamic_array<own::shared_owner<FSM::FSMState>> m_states;

	[[reflgen::ignore]]
	ce::dynamic_array<own::shared_owner<FSM::Transition>> m_transitions;

	[[reflgen::ignore]]
	FSM::FSMState* m_currentState = nullptr;

	[[reflgen::ignore]]
	BlackBoard m_localBB;
};
