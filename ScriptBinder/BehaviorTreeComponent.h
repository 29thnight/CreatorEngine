#pragma once
#include "Component.h"
#include "IAIComponent.h"
#include "BTHeader.h"

using namespace BT;

class BehaviorTreeComponent : public Component, public IAIComponent
{
public:
	BehaviorTreeComponent() = default;
	~BehaviorTreeComponent() = default;


	// IAIComponent �������̽� ����
	void Initialize() override {}
	void Tick(float deltaTime) override {
		if (m_root) {
			m_root->Tick(deltaTime,m_blackboard);
		}
	}

	// Behavior Tree ���� �޼���
	void SetRoot(BTNode::NodePtr root) { m_root = root; }
	BTNode::NodePtr GetRoot() const { return m_root; }

private:

	BlackBoard m_blackboard; // ������ ������
	BTNode::NodePtr m_root; // ��Ʈ ���
};

