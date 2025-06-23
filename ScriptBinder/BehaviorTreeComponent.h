#pragma once
#include "Component.h"
#include "IAIComponent.h"
#include "BTHeader.h"

namespace BT
{
	class BehaviorTreeComponent : public Component, public IAIComponent
	{
	public:
		BehaviorTreeComponent() = default;
		~BehaviorTreeComponent() = default;
		

		// IAIComponent �������̽� ����
		void Initialize() override;
		void Tick(float deltaTime) override {
			if (m_root) {
				m_root->Tick(m_blackboard);
			}
		}


		// Behavior Tree ���� �޼���
		void SetRoot(BTNode::Ptr root) { m_root = root; }
		BTNode::Ptr GetRoot() const { return m_root; }

	private:
		Blackboard m_blackboard; // ������ ������
		BTNode::Ptr m_root; // ��Ʈ ���
	};

}