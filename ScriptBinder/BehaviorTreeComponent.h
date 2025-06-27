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
   BehaviorTreeComponent() {
	   m_name = "BehaviorTreeComponent"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<BehaviorTreeComponent>();
	   Initialize();
   } virtual ~BehaviorTreeComponent() = default;

	[[Property]]
	std::string name; // 이름 속성


	// IAIComponent 인터페이스 구현
	void Initialize() override {
		if (!m_root)
		{
			m_root = std::make_shared<RootNode>();
		}
	}
	void Tick(float deltaTime) override {
		if (m_root) {
			m_root->Tick(deltaTime,m_blackboard);
		}
	}

	// Behavior Tree 관련 메서드
	//void SetRoot(BTNode::NodePtr root) { m_root = root; }
	BTNode::NodePtr GetRoot() const { return m_root; }

	//editor inspector 용
	ImVec2 GetNodePosition(const BT::BTNode::NodePtr& node) const;
	void   SetNodePosition(const BT::BTNode::NodePtr& node, const ImVec2& pos);

private:

	BlackBoard m_blackboard; // 블랙보드 데이터
	std::shared_ptr<RootNode> m_root; // 루트 노드

	//editor inspector 용
	std::unordered_map<BTNode*, ImVec2> _nodePositions;

	void* scriptInstance{ nullptr }; // 스크립트 인스턴스 포인터
	std::string typeName; // 스크립트 컴포넌트의 타입이름
};

