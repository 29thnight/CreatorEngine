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
	std::string name; // 스크립트 이름

	// IAIComponent 인터페이스 구현
	void Initialize() override;
	void Awake() override;
	void Update(float deltaSecond) override;
	void OnDistroy() override;

private:
	// Behavior Tree 관련 메서드
	BTNode::NodePtr BuildTree(const BTBuildGraph& graph);
	BTNode::NodePtr BuildTreeRecursively(const HashedGuid& nodeId, const BTBuildGraph& graph);

public:
	void GraphToBuild();
	void ClearTree() 
	{
		m_root.reset(); // Clear the root node
	}

public:
	// Behavior Tree의 GUID 직렬화 및 역직렬화를 위한 구조
	[[Property]]
	FileGuid m_BehaviorTreeGuid; // Behavior Tree의 GUID
	[[Property]]
	FileGuid m_BlackBoardGuid; // 블랙보드의 GUID

private:
	BlackBoard* m_pBlackboard; // 블랙보드 데이터
	BTNode::NodePtr m_root; // 루트 노드
	std::unordered_map<HashedGuid, BTNode::NodePtr> m_built; // 빌드된 노드들

};
//TODO : Open BT File, AIManager에서 BT 그래프 GUID 받아오기, 블랙보드도 마찬가지 -> Inspector에서 보여주기
