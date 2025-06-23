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
	// ���� ���� ������Ʈ
	m_currentState->Update(m_blackboard, deltaTime);

	// ���� �˻�
	for (const auto& transition : m_transitions) {

		if (transition->IsTriggered(m_blackboard)&&transition->GetTo()!=m_currentState) {
			// ���� ���� ����
			m_currentState->Exit(m_blackboard);
			// ���� ���·� ����
			m_currentState = transition->GetTo();
			m_currentState->Enter(m_blackboard);
			break; // �� ���� Tick���� �ϳ��� ���̸� ó��
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
