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
	return m_children.AddState(name);
}

void StateMachineComponent::RemoveState(FSM::FSMState* state)
{
	m_children.RemoveState(state);
	if (m_currentState == state)
	{
		m_currentState = nullptr; // Reset current state if it was removed
	}

}


FSM::Transition* StateMachineComponent::AddTransition(FSM::FSMState* from, FSM::FSMState* to, ConditionFunc condition)
{
	return m_children.AddTransition(from, to, std::move(condition));
}

void StateMachineComponent::RemoveTransition(FSM::Transition* transition)
{
	m_children.RemoveTransition(transition);
}


FSM::FSMState* StateMachineComponent::FindStateByName(const std::string& name) const
{
	return m_children.FindStateByName(name);
}
