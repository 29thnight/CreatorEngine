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
	std::string name; // �̸� �Ӽ�


	// IAIComponent �������̽� ����
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

	// Behavior Tree ���� �޼���
	//void SetRoot(BTNode::NodePtr root) { m_root = root; }
	BTNode::NodePtr GetRoot() const { return m_root; }

	//editor inspector ��
	ImVec2 GetNodePosition(const BT::BTNode::NodePtr& node) const;
	void   SetNodePosition(const BT::BTNode::NodePtr& node, const ImVec2& pos);

private:

	BlackBoard m_blackboard; // ������ ������
	std::shared_ptr<RootNode> m_root; // ��Ʈ ���

	//editor inspector ��
	std::unordered_map<BTNode*, ImVec2> _nodePositions;

	void* scriptInstance{ nullptr }; // ��ũ��Ʈ �ν��Ͻ� ������
	std::string typeName; // ��ũ��Ʈ ������Ʈ�� Ÿ���̸�
};

