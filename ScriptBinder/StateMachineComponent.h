#pragma once
#include "IAIComponent.h"
#include "Blackboard.h"
#include "FSMState.h"
#include "Transition.h"
#include "Component.h"
#include <vector>
#include <memory>

class StateMachineComponent : public Component, public IAIComponent
{
public:
	StateMachineComponent() = default;
	~StateMachineComponent() override = default;

	// IAIComponent �������̽� ����
	void Initialize() override;
	void Tick(float deltaTime) override;
	//todo : ����ȭ


	// ���� �ӽ� ���� �޼���
	FSMState* AddState(const std::string& name,
		FSMState::FSMStateCallback onEnter = nullptr,
		FSMState::FSMStateCallback onUpdate = nullptr,
		FSMState::FSMStateCallback onExit = nullptr);
	void AddTransition(FSMState* from, FSMState* to, Transition::Condition condition);
	void SetInitialState(FSMState* state);
	void RemoveState(FSMState* state) {
		//�����	��� ���� ����
		m_transitions.erase(
			std::remove_if(m_transitions.begin(), m_transitions.end(),
				[state](const std::unique_ptr<Transition>& transition) {
					return transition->GetFrom() == state || transition->GetTo() == state;
				}),
			m_transitions.end());

		//���� ����
		m_states.erase(
			std::remove_if(m_states.begin(), m_states.end(),
				[state](const std::unique_ptr<FSMState>& s) {
					return s.get() == state;
				}),
			m_states.end());
	}


	void RemoveTransition(Transition* tr) {
		m_transitions.erase(
			std::remove_if(m_transitions.begin(), m_transitions.end(),
				[tr](const std::unique_ptr<Transition>& transition) {
					return transition.get() == tr;
				}),
			m_transitions.end());
	}


	//editor access
	const std::vector<std::unique_ptr<FSMState>>& GetStates() const { return m_states; }
	const std::vector < std::unique_ptr<Transition>>& GetTransitions() const { return m_transitions; }

	FSMState* FindStateByID(int id) const {
		for (const auto& state : m_states) {
			if (state->GetID() == id) {
				return state.get();
			}
		}
		return nullptr;
	}

	Transition* FindTransitionByID(int id) const {
		for (const auto& transition : m_transitions) {
			if (transition->GetID() == id) {
				return transition.get();
			}
		}
		return nullptr;
	}

private:
	Blackboard	 m_blackboard; // ���� �ӽ��� ������
	FSMState*	 m_currentState{ nullptr }; // ���� ����
	std::vector<std::unique_ptr<FSMState>> m_states; // ���� ���
	std::vector<std::unique_ptr<Transition>> m_transitions; // ���� ���

	int m_nextStateID{ 0 }; // ���� ���� ID (�ڵ� ������)
	int m_nextTransitionID{ 0 }; // ���� ���� ID (�ڵ� ������)
};