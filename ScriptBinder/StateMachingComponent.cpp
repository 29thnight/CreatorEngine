#include "FSMState.h"
#include "Transition.h"
#include "StateMachingComponent.h"

void StateMachingComponent::Initialize()
{
	if (m_currentState)
	{
		m_currentState->Enter(m_localBB);
	}
}

void StateMachingComponent::Tick(float deltaTime)
{
	if (!m_currentState && !m_states.empty())
	{
		m_currentState = m_states.front().get();
	}

	if (m_currentState)
	{
		m_currentState->Update(m_localBB, deltaTime);
		// Check transitions
		for (const auto& transition : m_transitions)
		{
			if (transition->GetFrom() == m_currentState && (*transition)(m_localBB))
			{
				m_currentState->Exit(m_localBB);
				m_currentState = transition->GetTo();
				m_currentState->Enter(m_localBB);
				break; // Exit after a successful transition
			}
		}
	}
}

FSM::FSMState* StateMachingComponent::AddState(const std::string& name)
{
	m_states.emplace_back(std::make_unique<FSM::FSMState>(name));
	return m_states.back().get(); // Return the raw pointer to the state
}

void StateMachingComponent::RemoveState(FSM::FSMState* state)
{
	m_transitions.erase(
		std::remove_if(m_transitions.begin(), m_transitions.end(),
			[state](const std::unique_ptr<FSM::Transition>& transition) {
				return transition->GetFrom() == state || transition->GetTo() == state;
			}),
		m_transitions.end());
	m_states.erase(
		std::remove_if(m_states.begin(), m_states.end(),
			[state](const std::unique_ptr<FSM::FSMState>& s) {
				return s.get() == state;
			}),
		m_states.end());
	if (m_currentState == state)
	{
		m_currentState = nullptr; // Reset current state if it was removed
	}

}


FSM::Transition* StateMachingComponent::AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition)
{
	m_transitions.push_back(std::make_unique<FSM::Transition>(from, to, condition));
	return m_transitions.back().get(); // Return the raw pointer to the transition
}

void StateMachingComponent::RemoveTransition(FSM::Transition* transition)
{
	m_transitions.erase(
		std::remove_if(m_transitions.begin(), m_transitions.end(),
			[transition](const std::unique_ptr<FSM::Transition>& t) {
				return t.get() == transition;
			}),
		m_transitions.end());
}


FSM::FSMState* StateMachingComponent::FindStateByName(const std::string& name) const
{
	for (const auto& state : m_states)
	{
		if (state->GetName() == name)
		{
			return state.get();
		}
	}
	return nullptr; // Return nullptr if no state with the given name is found
}


