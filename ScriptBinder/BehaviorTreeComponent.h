#pragma once
#include "imgui-node-editor\imgui_node_editor.h"
#include "IUpdatable.h"
#include "Component.h"
#include "IAIComponent.h"
#include "BTHeader.h"
#include "AIManager.h"
#include "IAwakable.h"
#include "IOnDistroy.h"
#include "BehaviorTreeComponent.generated.h"

using namespace BT;

class BehaviorTreeComponent : 
	public Component, public IAIComponent, public IUpdatable, public IAwakable, public IOnDistroy
{
public:
   ReflectBehaviorTreeComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(BehaviorTreeComponent)

	[[Property]]
	std::string name; // ��ũ��Ʈ �̸�

	// IAIComponent �������̽� ����
	void Initialize() override;
	void Awake() override;
	void Update(float deltaSecond) override;
	void OnDistroy() override;

private:
	// Behavior Tree ���� �޼���
	BTNode::NodePtr BuildTree(const BTBuildGraph& graph);
	BTNode::NodePtr BuildTreeRecursively(const HashedGuid& nodeId, const BTBuildGraph& graph);

public:
	void GraphToBuild();
	void ClearTree() 
	{
		m_root.reset(); // Clear the root node
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
//TODO : Open BT File, AIManager���� BT �׷��� GUID �޾ƿ���, �����嵵 �������� -> Inspector���� �����ֱ�
