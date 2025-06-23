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
		

		// IAIComponent 인터페이스 구현
		void Initialize() override;
		void Tick(float deltaTime) override {
			if (m_root) {
				m_root->Tick(m_blackboard);
			}
		}


		// Behavior Tree 관련 메서드
		void SetRoot(BTNode::Ptr root) { m_root = root; }
		BTNode::Ptr GetRoot() const { return m_root; }

	private:
		Blackboard m_blackboard; // 블랙보드 데이터
		BTNode::Ptr m_root; // 루트 노드
	};

}