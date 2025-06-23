#include "StateMachineComponent.h"

void StateMachineComponent::Initialize()
{
	if (m_currentState)
	{
		m_currentState->Enter(m_blackboard);
	}
}

void StateMachineComponent::Tick(float deltaTime)
{
	if (!m_currentState) {
		return;
	}
	// 현재 상태 업데이트
	m_currentState->Update(m_blackboard, deltaTime);

	// 전이 검사
	for (const auto& transition : m_transitions) {

		if (transition->IsTriggered(m_blackboard)&&transition->GetTo()!=m_currentState) {
			// 현재 상태 종료
			m_currentState->Exit(m_blackboard);
			// 다음 상태로 전이
			m_currentState = transition->GetTo();
			m_currentState->Enter(m_blackboard);
			break; // 한 번의 Tick에서 하나의 전이만 처리
		}
	}

}

FSMState* StateMachineComponent::AddState(const std::string& name, FSMState::FSMStateCallback onEnter, FSMState::FSMStateCallback onUpdate, FSMState::FSMStateCallback onExit)
{
	auto state = std::make_unique<FSMState>(name, onEnter, onUpdate, onExit);
	state->SetID(m_nextStateID++);
	m_states.push_back(std::move(state));
	return m_states.back().get();
}

void StateMachineComponent::AddTransition(FSMState* from, FSMState* to, Transition::Condition condition)
{
	auto transition = std::make_unique<Transition>(from, to, condition);
	transition->SetID(m_nextTransitionID++);
	m_transitions.push_back(std::move(transition));
}

void StateMachineComponent::SetInitialState(FSMState* state)
{
	m_currentState = state;
}
