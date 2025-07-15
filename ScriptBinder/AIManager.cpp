#ifndef DYNAMICCPP_EXPORTS
#include "AIManager.h"
#include "BehaviorTreeComponent.h"
#include "StateMachineComponent.h"

BlackBoard* AIManager::CreateBlackBoard(const std::string& aiName)
{
	// �����尡 �̹� �����ϴ��� Ȯ��
	if (m_blackBoardFind.find(aiName) != m_blackBoardFind.end())
		return m_blackBoardFind[aiName];

	m_blackBoards.emplace_back();
	m_blackBoardFind.emplace(aiName, &m_blackBoards.back());

	return &m_blackBoards.back();
}

void AIManager::RemoveBlackBoard(const std::string& aiName)
{
	auto it = m_blackBoardFind.find(aiName);
	if (it != m_blackBoardFind.end())
	{
		std::erase_if(m_blackBoards, [&](const BlackBoard& bb) { return &bb == it->second; });
		m_blackBoardFind.erase(it);
	}
}

void AIManager::RegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	// GameObject�� AI ������Ʈ ����
	m_aiComponentMap[gameObject] = aiComponent;
}

void AIManager::UnRegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	auto it = m_aiComponentMap.find(gameObject);
	if (it != m_aiComponentMap.end() && it->second == aiComponent)
	{
		m_aiComponentMap.erase(it);
	}
}

BT::BTNode::NodePtr AIManager::CreateNode(const std::string_view& nodeName)
{
	return BTNodeFactory->Create(nodeName.data());
}

void AIManager::ClearTreeInAIComponent()
{
	for (auto& [gameObject, aiComponent] : m_aiComponentMap)
	{
		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->ClearTree();
		}
	}
}

void AIManager::InitalizeBehaviorTreeSystem()
{
	// Behavior Tree ��� ���丮 �ʱ�ȭ
	BTNodeFactory->Clear();

	// �Ķ���Ͱ� ���� ��� ���
	BTNodeFactory->Register("RootSequence", []()
	{
		return std::make_shared<BT::SequenceNode>("RootSequence");
	});

	BTNodeFactory->Register("Sequence", []()
	{
		return std::make_shared<BT::SequenceNode>("Sequence");
	});

	BTNodeFactory->Register("Selector", []()
	{
		return std::make_shared<BT::SelectorNode>("Selector");
	});

	// InverterNode ���� Decorator ���
	BTNodeFactory->Register("Inverter", []()
	{
		// Decorator�� �ڽ� ��带 ��������, �ڽ� ������ ���丮 ���� �������� ó��
		return std::make_shared<BT::InverterNode>("Inverter", nullptr);
	});

	//TODO :  DLL���� ��� �ε��ؼ� ����� ��
	int actionSize = 0;
	auto nameArr = ScriptManager->ListBTActionNodeNames(&actionSize);

	for (int i = 0; i < actionSize; ++i)
	{
		const std::string& actionName = nameArr[i];
		m_btActionNodeNames.push_back(actionName);
	}

	int conditionSize = 0;
	auto conditionArr = ScriptManager->ListBTConditionNodeNames(&conditionSize);

	for (int i = 0; i < conditionSize; ++i)
	{
		const std::string& conditionName = conditionArr[i];
		m_btConditionNodeNames.push_back(conditionName);
	}

	for (const auto& actionName : m_btActionNodeNames)
	{
		BTNodeFactory->Register(actionName, [actionName]()
		{
			return std::shared_ptr<BT::ActionNode>(
				ScriptManager->CreateActionNode(actionName.c_str()),
				[](BT::ActionNode* ptr)
				{
					ScriptManager->DestroyActionNode(ptr);
				});
		});
	}

	for (const auto& conditionName : m_btConditionNodeNames)
	{
		BTNodeFactory->Register(conditionName, [conditionName]()
		{
			return std::shared_ptr<BT::ConditionNode>(
				ScriptManager->CreateConditionNode(conditionName.c_str()),
				[](BT::ConditionNode* ptr)
				{
					ScriptManager->DestroyConditionNode(ptr);
				});
		});
	}
}

#endif // DYNAMICCPP_EXPORTS