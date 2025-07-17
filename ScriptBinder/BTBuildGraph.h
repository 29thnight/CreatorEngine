#pragma once
#include "BTBuildNode.h"
#include "BTBuildGraph.generated.h"
#include "ReflectionYml.h"

struct BTBuildGraph
{
	std::unordered_map<HashedGuid, BTBuildNode*> Nodes;
	BTBuildNode* SelectedNode{ nullptr };

	[[Property]]
	std::vector<BTBuildNode> NodeList;

   ReflectBTBuildGraph
	[[Serializable]]
	BTBuildGraph()
	{
	   NodeList.reserve(200); // 초기 노드 리스트 크기 예약

		BTBuildNode rootNode;
		rootNode.ID = make_guid();
		rootNode.Type = BehaviorNodeType::Sequence; // 기본적으로 Sequence로 설정
		rootNode.Name = "RootSequence";
		rootNode.IsRoot = true; // 루트 노드로 설정
		rootNode.Position = Mathf::Vector2(0, 0); // 초기 위치 설정
		rootNode.InputPinId = ed::PinId(rootNode.ID.m_ID_Data << 1);
		rootNode.OutputPinId = ed::PinId((rootNode.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(rootNode);
		Nodes[rootNode.ID] = &NodeList.back();
		SelectedNode = &NodeList.back(); // 루트 노드를 선택된 노드로 설정
	}

	~BTBuildGraph()
	{
		NodeList.clear();
		Nodes.clear();
	}

	BTBuildNode* CreateNode(const BehaviorNodeType type, const std::string_view& name, Mathf::Vector2 pos = { 0, 0 })
	{
		BTBuildNode node;
		node.ID = make_guid();
		node.Type = type;
		node.Name = std::string(name);
		node.IsRoot = false; // 기본적으로 루트가 아님
		node.Position = pos; // 초기 위치 설정
		node.InputPinId = ed::PinId(node.ID.m_ID_Data << 1);
		node.OutputPinId = ed::PinId((node.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(node);
		Nodes[node.ID] = &NodeList.back();

		return Nodes[node.ID];
	}

	void AddChildNode(BTBuildNode* childNode)
	{
		if (nullptr == SelectedNode) return;

		const auto& selectedNodeType = SelectedNode->Type;
		auto& selectedNodeChildContainer = SelectedNode->Children;
		const auto& childNodeID = childNode->ID;
		const auto& selectedNodeID = SelectedNode->ID;
		auto& childNodeParentID = childNode->ParentID;

		if (BT::IsDecoratorNode(selectedNodeType) && 0 < selectedNodeChildContainer.size()) return; // Decorator 노드는 자식이 최대 1개만 허용됨

		selectedNodeChildContainer.push_back(childNodeID);

		childNodeParentID = selectedNodeID;
		SelectedNode = childNode; // 새로 추가한 노드를 선택된 노드로 설정
	}

	void PickNode(const HashedGuid& id)
	{
		if(Nodes.find(id) != Nodes.end())
		{
			SelectedNode = Nodes[id];
		}
	}

	void DeleteNode(const HashedGuid& id)
	{
		auto it = Nodes.find(id);
		if (it != Nodes.end())
		{
			BTBuildNode* node = it->second;
			if (node->IsRoot) return; // 루트 노드는 삭제할 수 없음

			// 모든 링크 제거
			for (auto& [otherId, otherNode] : Nodes)
			{
				if (otherNode->ParentID == id)
				{
					otherNode->ParentID = HashedGuid(); // 부모 ID 초기화
				}
				std::erase_if(otherNode->Children, 
					[&id](const HashedGuid& childId) { return childId == id; }); // 자식 ID 제거
			}

			NodeList.erase(std::remove_if(NodeList.begin(), NodeList.end(),
				[&id](const BTBuildNode& node) { return node.ID == id; }), NodeList.end());
			Nodes.erase(it);
		}
	}

	void Clear()
	{
		NodeList.clear();
		Nodes.clear();

		BTBuildNode rootNode;
		rootNode.ID = make_guid();
		rootNode.Type = BehaviorNodeType::Sequence; // 기본적으로 Sequence로 설정
		rootNode.Name = "RootSequence";
		rootNode.IsRoot = true; // 루트 노드로 설정
		rootNode.Position = Mathf::Vector2(0, 0); // 초기 위치 설정
		rootNode.InputPinId = ed::PinId(rootNode.ID.m_ID_Data << 1);
		rootNode.OutputPinId = ed::PinId((rootNode.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(rootNode);
		Nodes[rootNode.ID] = &NodeList.back();
		SelectedNode = &NodeList.back(); // 루트 노드를 선택된 노드로 설정
	}

	void CleanUp()
	{
		NodeList.clear();
		Nodes.clear();
		SelectedNode = nullptr;
	}

	void DeserializeSingleNode(const YAML::Node& node)
	{
		BTBuildNode out;

		Meta::Deserialize(&out, node);

		// Reconstruct PinID
		out.InputPinId = ed::PinId(out.ID.m_ID_Data << 1);
		out.OutputPinId = ed::PinId((out.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(out);
		Nodes[out.ID] = &NodeList.back();

		if (out.IsRoot)
		{
			SelectedNode = &NodeList.back(); // 루트 노드를 선택된 노드로 설정
		}
	}

	HashedGuid GetRootID() const
	{
		for (auto& [id, node] : Nodes)
		{
			if (node->IsRoot)
				return id;
		}
		return HashedGuid();
	}

	std::vector<std::string> GetRegisteredKey() const
	{
		// 빌드 메뉴에서 사용할 노드 타입 목록 반환 : 
		// 스크립트가 필요한 Action 노드 및 Condition 노드는 외부에서 따로 처리함.
		return {
			"Sequence",
			"Selector",
			"Parallel",
			"Inverter",
			"ConditionDecorator",
			"Action",
			"Condition",
		};
	}
};
