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
	   NodeList.reserve(200); // �ʱ� ��� ����Ʈ ũ�� ����

		BTBuildNode rootNode;
		rootNode.ID = make_guid();
		rootNode.Type = BehaviorNodeType::Sequence; // �⺻������ Sequence�� ����
		rootNode.Name = "RootSequence";
		rootNode.IsRoot = true; // ��Ʈ ���� ����
		rootNode.Position = Mathf::Vector2(0, 0); // �ʱ� ��ġ ����
		rootNode.InputPinId = ed::PinId(rootNode.ID.m_ID_Data << 1);
		rootNode.OutputPinId = ed::PinId((rootNode.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(rootNode);
		Nodes[rootNode.ID] = &NodeList.back();
		SelectedNode = &NodeList.back(); // ��Ʈ ��带 ���õ� ���� ����
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
		node.IsRoot = false; // �⺻������ ��Ʈ�� �ƴ�
		node.Position = pos; // �ʱ� ��ġ ����
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

		if (BT::IsDecoratorNode(selectedNodeType) && 0 < selectedNodeChildContainer.size()) return; // Decorator ���� �ڽ��� �ִ� 1���� ����

		selectedNodeChildContainer.push_back(childNodeID);

		childNodeParentID = selectedNodeID;
		SelectedNode = childNode; // ���� �߰��� ��带 ���õ� ���� ����
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
			if (node->IsRoot) return; // ��Ʈ ���� ������ �� ����

			// ��� ��ũ ����
			for (auto& [otherId, otherNode] : Nodes)
			{
				if (otherNode->ParentID == id)
				{
					otherNode->ParentID = HashedGuid(); // �θ� ID �ʱ�ȭ
				}
				std::erase_if(otherNode->Children, 
					[&id](const HashedGuid& childId) { return childId == id; }); // �ڽ� ID ����
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
		rootNode.Type = BehaviorNodeType::Sequence; // �⺻������ Sequence�� ����
		rootNode.Name = "RootSequence";
		rootNode.IsRoot = true; // ��Ʈ ���� ����
		rootNode.Position = Mathf::Vector2(0, 0); // �ʱ� ��ġ ����
		rootNode.InputPinId = ed::PinId(rootNode.ID.m_ID_Data << 1);
		rootNode.OutputPinId = ed::PinId((rootNode.ID.m_ID_Data << 1) | 1);

		NodeList.push_back(rootNode);
		Nodes[rootNode.ID] = &NodeList.back();
		SelectedNode = &NodeList.back(); // ��Ʈ ��带 ���õ� ���� ����
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
			SelectedNode = &NodeList.back(); // ��Ʈ ��带 ���õ� ���� ����
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
		// ���� �޴����� ����� ��� Ÿ�� ��� ��ȯ : 
		// ��ũ��Ʈ�� �ʿ��� Action ��� �� Condition ���� �ܺο��� ���� ó����.
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
