#pragma once;
#include <functional>
#include <string>
#include "Blackboard.h"

class FSMState
{
public:
	using FSMStateCallback = std::function<void(Blackboard&)>;

	FSMState(const std::string& name,
		FSMStateCallback onEnter = nullptr,
		FSMStateCallback onUpdate = nullptr,
		FSMStateCallback onExit = nullptr)
		: m_name(name), m_onEnter(onEnter), m_onUpdate(onUpdate), m_onExit(onExit) {
	}

	void SetID(int id) {
		m_id = id;
	}

	int GetID() const {
		return m_id;
	}

	void SetName(const std::string& name) {
		m_name = name;
	}

	const std::string& GetName() const {
		return m_name;
	}

	void Enter(Blackboard& bb) {
		if (m_onEnter) {
			m_onEnter(bb);
		}
	}

	void Update(Blackboard& bb,float dt) {
		if (m_onUpdate) {
			m_onUpdate(bb);
		}
	}

	void Exit(Blackboard& bb) {
		if (m_onExit) {
			m_onExit(bb);
		}
	}

	void SetPosition(const ImVec2& position) {
		m_position = position;
	}

	ImVec2 GetPosition() const {
		return m_position;
	}


private:
	int m_id = 0;
	std::string m_name; // ���� �̸�
	FSMStateCallback m_onEnter; // ���� ���� �� ȣ��Ǵ� �ݹ�
	FSMStateCallback m_onUpdate; // ���� ������Ʈ �� ȣ��Ǵ� �ݹ�
	FSMStateCallback m_onExit; // ���� ���� �� ȣ��Ǵ� �ݹ�

	ImVec2 m_position{ 0.0f, 0.0f }; // ������ ��ġ (�������)
};

