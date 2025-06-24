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


	// IAIComponent 인터페이스 구현
	void Initialize() override {}
	void Tick(float deltaTime) override {
		if (m_root) {
			m_root->Tick(deltaTime,m_blackboard);
		}
	}

	// Behavior Tree 관련 메서드
	void SetRoot(BTNode::NodePtr root) { m_root = root; }
	BTNode::NodePtr GetRoot() const { return m_root; }

private:

	BlackBoard m_blackboard; // 블랙보드 데이터
	BTNode::NodePtr m_root; // 루트 노드
};

