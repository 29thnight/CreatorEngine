#pragma once
#include "imgui-node-editor\imgui_node_editor.h"
#include "Component.h"
#include "IAIComponent.h"
#include "NodeFactory.h"
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
			m_root = std::make_shared<SequenceNode>("RootSequence");
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
	
	BTNode::NodePtr FindNodeByPin(ax::NodeEditor::PinId pin);


	BTNode::NodePtr CreateNode(const std::string& key,const json& data = {}) {
		return BTNodeFactory->Create(key, data);
	}

	void DeleteNode(const BTNode::NodePtr& node) {
		if (node) {
			// Remove the node from the tree
			// This is a placeholder, actual implementation may vary
			if (node == m_root) return;
			
			RemoveChildFromParent(node); // Remove from parent
			

			_nodePositions.erase(node.get()); // Remove position tracking
		}
	}
	void RemoveChildFromParent(const BTNode::NodePtr& target) {
		DFS(m_root, [&](const BTNode::NodePtr& node) {
			if (auto compositeNode = std::dynamic_pointer_cast<CompositeNode>(node)) {
				auto& children = compositeNode->GetChildren();
				children.erase(std::remove(children.begin(), children.end(), target), children.end());
			}
			else if (auto decoratorNode = std::dynamic_pointer_cast<DecoratorNode>(node)) {
				if (decoratorNode->GetChild() == target) {
					decoratorNode->SetChild(nullptr); // Remove child from decorator
				}
			}
		});
	}

private:

	BlackBoard m_blackboard; // 블랙보드 데이터
	BTNode::NodePtr m_root; // 루트 노드

	//editor inspector 용
	std::unordered_map<BTNode*, ImVec2> _nodePositions;

	void* scriptInstance{ nullptr }; // 스크립트 인스턴스 포인터
	std::string typeName; // 스크립트 컴포넌트의 타입이름
};

