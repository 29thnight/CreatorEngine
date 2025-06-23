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

	// IAIComponent 인터페이스 구현
	void Initialize() override;
	void Tick(float deltaTime) override;
	//todo : 직렬화


	// 상태 머신 관련 메서드
	FSMState* AddState(const std::string& name,
		FSMState::FSMStateCallback onEnter = nullptr,
		FSMState::FSMStateCallback onUpdate = nullptr,
		FSMState::FSMStateCallback onExit = nullptr);
	void AddTransition(FSMState* from, FSMState* to, Transition::Condition condition);
	void SetInitialState(FSMState* state);
	void RemoveState(FSMState* state) {
		//연결된	모든 전이 제거
		m_transitions.erase(
			std::remove_if(m_transitions.begin(), m_transitions.end(),
				[state](const std::unique_ptr<Transition>& transition) {
					return transition->GetFrom() == state || transition->GetTo() == state;
				}),
			m_transitions.end());

		//상태 제거
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
	Blackboard	 m_blackboard; // 상태 머신의 블랙보드
	FSMState*	 m_currentState{ nullptr }; // 현재 상태
	std::vector<std::unique_ptr<FSMState>> m_states; // 상태 목록
	std::vector<std::unique_ptr<Transition>> m_transitions; // 전이 목록

	int m_nextStateID{ 0 }; // 다음 상태 ID (자동 증가용)
	int m_nextTransitionID{ 0 }; // 다음 전이 ID (자동 증가용)
};