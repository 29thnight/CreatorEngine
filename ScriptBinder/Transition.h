#pragma once
#include <functional>
#include "Blackboard.h"
#include "FSMState.h"

class Transition
{
public:
	using Condition = std::function<bool(const Blackboard&)>;

	Transition(FSMState* from, FSMState* to, Condition condition)
		: m_from(from), m_to(to), m_condition(condition) {
	}

	void SetID(int id) {
		m_id = id;
	}

	int GetID() const {
		return m_id;
	}

	FSMState* GetFrom() const {
		return m_from;
	}

	FSMState* GetTo() const {
		return m_to;
	}

	bool IsTriggered(const Blackboard& bb) const {
		return m_condition ? m_condition(bb) : false;
	}


private:
	int m_id = 0;

	FSMState* m_from;
	FSMState* m_to;
	Condition m_condition;
};