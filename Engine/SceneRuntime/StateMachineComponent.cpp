#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "FSMState.h"
#include "Transition.h"
#include "StateMachineComponent.h"
#include "AIManager.h"

void StateMachineComponent::Initialize()
{
	m_aiType = AIType::FSM;
	if (m_currentState)
	{
		m_currentState->Enter(m_localBB);
	}
}

void StateMachineComponent::OnAddedToScene()
{
	AIManagers->RegisterAIComponent(GetOwner(), this);
}

void StateMachineComponent::OnRemovingFromScene()
{
	AIManagers->UnRegisterAIComponent(GetOwner(), this);
}

//void StateMachineComponent::Tick(float deltaTime)
//{
//	if (!m_currentState && !m_states.empty())
//	{
//		m_currentState = m_states.front().operator->();
//	}
//
//	if (m_currentState)
//	{
//		m_currentState->Update(m_localBB, deltaTime);
//		// Check transitions
//		for (const auto& transition : m_transitions)
//		{
//			if (transition->GetFrom() == m_currentState && (*transition)(m_localBB))
//			{
//				m_currentState->Exit(m_localBB);
//				m_currentState = transition->GetTo();
//				m_currentState->Enter(m_localBB);
//				break; // Exit after a successful transition
//			}
//		}
//	}
//}

FSM::FSMState* StateMachineComponent::AddState(const std::string& name)
{
	m_states.emplace_back(own::make_shared<FSM::FSMState>(name));
	return m_states.back().operator->(); // Return the raw pointer to the state
}

void StateMachineComponent::RemoveState(FSM::FSMState* state)
{
	m_transitions.erase(
		std::remove_if(m_transitions.begin(), m_transitions.end(),
			[state](const own::shared_owner<FSM::Transition>& transition) {
				return transition->GetFrom() == state || transition->GetTo() == state;
			}),
		m_transitions.end());
	m_states.erase(
		std::remove_if(m_states.begin(), m_states.end(),
			[state](const own::shared_owner<FSM::FSMState>& s) {
				return s.operator->() == state;
			}),
		m_states.end());
	if (m_currentState == state)
	{
		m_currentState = nullptr; // Reset current state if it was removed
	}

}


FSM::Transition* StateMachineComponent::AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition)
{
	m_transitions.push_back(own::make_shared<FSM::Transition>(from, to, condition));
	return m_transitions.back().operator->(); // Return the raw pointer to the transition
}

void StateMachineComponent::RemoveTransition(FSM::Transition* transition)
{
	m_transitions.erase(
		std::remove_if(m_transitions.begin(), m_transitions.end(),
			[transition](const own::shared_owner<FSM::Transition>& t) {
				return t.operator->() == transition;
			}),
		m_transitions.end());
}


FSM::FSMState* StateMachineComponent::FindStateByName(const std::string& name) const
{
	for (const auto& state : m_states)
	{
		if (state->GetName() == name)
		{
			return state.operator->();
		}
	}
	return nullptr; // Return nullptr if no state with the given name is found
}
