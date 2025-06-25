#pragma once
#include "Component.h"
#include "IAIComponent.h"
#include "BTHeader.h"
#include "BehaviorTreeComponent.generated.h"

using namespace BT;

class BehaviorTreeComponent : public Component, public IAIComponent
{
public:
   ReflectBehaviorTreeComponent
		[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(BehaviorTreeComponent)

	[[Property]]
	std::string name; // �̸� �Ӽ�


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

