#pragma once
#include "imgui-node-editor\imgui_node_editor.h"
#include "Component.h"
#include "IAIComponent.h"
#include "BTHeader.h"
#include "AIManager.h"
#include "IRegistableEvent.h"
#include "BehaviorTreeComponent.generated.h"

using namespace BT;

class BehaviorTreeComponent : 
	public Component, public IAIComponent, public RegistableEvent<BehaviorTreeComponent>
{
public:
   ReflectBehaviorTreeComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(BehaviorTreeComponent)

	[[Property]]
	std::string name; // BT ���� �̸�
	[[Property]]
	std::string blackBoardName;

	// IAIComponent �������̽� ����
	void Initialize() override;
	void Awake() override;
	void InternalAIUpdate(float deltaSecond) override;
	void OnDestroy() override;
	BlackBoard* GetBlackBoard();
private:
	// Behavior Tree ���� �޼���
	BTNode::NodePtr BuildTree(const BTBuildGraph& graph);
	BTNode::NodePtr BuildTreeRecursively(const HashedGuid& nodeId, const BTBuildGraph& graph);
public:
	void GraphToBuild();
	void ClearTree() 
	{
		m_built.clear(); // Clear the built nodes map
		m_root.reset(); // Clear the root node
		m_built.clear();
	}

public:
	// Behavior Tree�� GUID ����ȭ �� ������ȭ�� ���� ����
	[[Property]]
	FileGuid m_BehaviorTreeGuid; // Behavior Tree�� GUID
	[[Property]]
	FileGuid m_BlackBoardGuid; // �������� GUID
private:
	BlackBoard* m_pBlackboard; // ������ ������
	BTNode::NodePtr m_root; // ��Ʈ ���
	std::unordered_map<HashedGuid, BTNode::NodePtr> m_built; // ����� ����
};
