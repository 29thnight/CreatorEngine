#include "BehaviorTreeComponent.h"
#include "SceneManager.h"
#include "NodeFactory.h"

void BehaviorTreeComponent::Initialize()
{
	AIManagers->RegisterAIComponent(GetOwner(), this);
	if (m_BlackBoardGuid != nullFileGuid)
	{
		//TODO: 블랙보드 인스턴스 생성 준비
		m_pBlackboard = new BlackBoard();

		file::path blackBoardPath = DataSystems->GetFilePath(m_BlackBoardGuid);
		if (!file::exists(blackBoardPath))
		{
			Debug->LogError("Blackboard File not Exists");
		}
		else
		{
			std::string _blackBoardName = blackBoardPath.stem().string();
			m_pBlackboard->Deserialize(_blackBoardName);
		}

	}
	else
	{
		Debug->LogError("Blackboard GUID is invalid.");
	}

	if (m_BehaviorTreeGuid != nullFileGuid)
	{
		GraphToBuild();
	}
	else
	{
		Debug->LogError("Behavior Tree GUID is invalid.");
	}
}

void BehaviorTreeComponent::Awake()
{
	Initialize();
}

void BehaviorTreeComponent::InternalAIUpdate(float deltaSecond)
{
	if (GetOwner()->m_isEnabled == false) return;

	if (m_root && m_pBlackboard && SceneManagers->m_isGameStart)
	{
		m_root->Tick(deltaSecond, *m_pBlackboard);
	}
}

void BehaviorTreeComponent::OnDestroy()
{
	if (m_pBlackboard)
	{
		/*m_pBlackboard = nullptr;*/
		Memory::SafeDelete(m_pBlackboard);
		AIManagers->RemoveBlackBoard(name);
	}
	m_built.clear(); // 재빌드 대비
	AIManagers->UnRegisterAIComponent(GetOwner(), this);
}

BTNode::NodePtr BehaviorTreeComponent::BuildTree(const BTBuildGraph& graph)
{
	HashedGuid rootID = graph.GetRootID();
	if (rootID.m_ID_Data == HashedGuid::INVAILD_ID)
		throw std::runtime_error("BTTreeBuilder: No root node found.");

	m_built.clear(); // 재빌드 대비
	return BuildTreeRecursively(rootID, graph);
}

BTNode::NodePtr BehaviorTreeComponent::BuildTreeRecursively(const HashedGuid& nodeId, const BTBuildGraph& graph)
{
	if (m_built.contains(nodeId))
		return m_built[nodeId];

	auto it = graph.Nodes.find(nodeId);
	if (it == graph.Nodes.end())
		throw std::runtime_error("BTTreeBuilder: Node ID not found.");

	const BTBuildNode* buildNode = it->second;

	BTNode::NodePtr node;

	if(!buildNode->ScriptName.empty())
	{
		node = AIManagers->CreateNode(buildNode->ScriptName);
	}
	else
	{
		node = AIManagers->CreateNode(buildNode->Name);
	}

	if (!node)
		throw std::runtime_error("BTTreeBuilder: Failed to create node for: " + buildNode->Name);

	m_built[nodeId] = node;

	node->SetOwner(GetOwner());

	//for (const auto& childID : buildNode->Children)
	//{
	//	BTNode::NodePtr childNode = BuildTreeRecursively(childID, graph);

	//	if (auto composite = std::dynamic_pointer_cast<BT::CompositeNode>(node))
	//	{
	//		composite->AddChild(childNode);
	//	}
	//	else if (auto decorator = std::dynamic_pointer_cast<BT::DecoratorNode>(node))
	//	{
	//		if (!decorator->IsOutpinConnected())
	//		{
	//			decorator->SetChild(childNode);
	//		}
	//		else
	//		{
	//			Debug->LogWarning("Decorator node already has a child: " + node->GetName());
	//		}
	//	}
	//	else
	//	{
	//		// Action / Condition node → 자식 없음
	//	}
	//}

	for (size_t i = 0; i < buildNode->Children.size(); ++i)
	{
		const auto& childID = buildNode->Children[i];
		BTNode::NodePtr childNode = BuildTreeRecursively(childID, graph);

		// NEW: Check for WeightedSelector
		if (auto weightedSelector = std::dynamic_pointer_cast<BT::WeightedSelectorNode>(node))
		{
			// Ensure weights are available
			if (i < buildNode->ChildWeights.size())
			{
				float weight = buildNode->ChildWeights[i];
				weightedSelector->AddChildWithWeight(childNode, weight);
			}
			else
			{
				// Handle error or provide a default weight
				weightedSelector->AddChildWithWeight(childNode, 1.0f);
				Debug->LogWarning("WeightedSelector node '" + buildNode->Name + "' is missing a weight for a child. Defaulting to 1.0f.");
			}
		}
		else if (auto composite = std::dynamic_pointer_cast<BT::CompositeNode>(node))
		{
			composite->AddChild(childNode);
		}
		else if (auto decorator = std::dynamic_pointer_cast<BT::DecoratorNode>(node))
		{
			if (!decorator->IsOutpinConnected())
			{
				decorator->SetChild(childNode);
			}
			else
			{
				Debug->LogWarning("Decorator node already has a child: " + node->GetName());
			}
		}
		// Action / Condition nodes have no children, so no special handling needed here
	}



	return node;
}

BlackBoard* BehaviorTreeComponent::GetBlackBoard()
{
	return m_pBlackboard;
}

void BehaviorTreeComponent::GraphToBuild()
{
	file::path BTpath = DataSystems->GetFilePath(m_BehaviorTreeGuid);
	if (!BTpath.empty() && file::exists(BTpath))
	{
		BTBuildGraph graph;
		auto node = MetaYml::LoadFile(BTpath.string());
		const MetaYml::Node& nodeList = node["NodeList"];
		if (nodeList && nodeList.IsSequence())
		{
			graph.CleanUp();
			for (const auto& node : nodeList)
			{
				graph.DeserializeSingleNode(node);
			}
		}

		m_root = BuildTree(graph);
	}
	else
	{
		Debug->LogError("Behavior Tree file does not exist: " + BTpath.string());
	}
}


