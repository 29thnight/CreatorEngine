#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "HotLoadSystem.h"
#include "NodeFactory.h"
#include "IAIComponent.h"

class StateMachineComponent;
class BehaviorTreeComponent;
class AIManager : public Singleton<AIManager>
{
private:
	friend class Singleton;
	AIManager() = default;
	~AIManager() = default;

public:
	BlackBoard& GetGlobalBlackBoard()
	{
		return m_globalBB;
	}

	BlackBoard* CreateBlackBoard(const std::string& aiName); // �̰͵� �ٽ� �ۼ� : �����忡 ���� ���丮�� �ʿ��� ����

	void RemoveBlackBoard(const std::string& aiName); // �̰͵� �ٽ� �ۼ�

	void RegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);

	void UnRegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);

	void InternalAIUpdate(float deltaSeconds);

	BT::BTNode::NodePtr CreateNode(std::string_view nodeName);

	void ClearTreeInAIComponent();

	void InitalizeBehaviorTreeSystem();

	bool IsActionNodeRegistered(const std::string& actionName) const
	{
		return std::find(m_btActionNodeNames.begin(), 
			m_btActionNodeNames.end(), actionName) != m_btActionNodeNames.end();
	}

	bool IsConditionNodeRegistered(const std::string& conditionName) const
	{
		return std::find(m_btConditionNodeNames.begin(), 
			m_btConditionNodeNames.end(), conditionName) != m_btConditionNodeNames.end();
	}

	bool IsConditionDecoratorRegistered(const std::string& conditionDecName) const
	{
		return std::find(m_btConditionDecoratorNodeNames.begin(),
			m_btConditionDecoratorNodeNames.end(), conditionDecName) != m_btConditionDecoratorNodeNames.end();
	}

	bool IsScriptNodeRegistered(const std::string& scriptName)
	{
		return	IsActionNodeRegistered(scriptName)				|| 
				IsConditionNodeRegistered(scriptName)			|| 
				IsConditionDecoratorRegistered(scriptName);
	}

	const std::vector<std::string>& GetActionNodeNames() const
	{
		return m_btActionNodeNames;
	}

	const std::vector<std::string>& GetConditionNodeNames() const
	{
		return m_btConditionNodeNames;
	}

	const std::vector<std::string>& GetConditionDecoratorNodeNames() const
	{
		return m_btConditionDecoratorNodeNames;
	}

private:
	friend class HotLoadSystem;

	std::vector<std::string> m_btActionNodeNames{};
	std::vector<std::string> m_btConditionNodeNames{};
	std::vector<std::string> m_btConditionDecoratorNodeNames{};
	Core::Delegate<void, float>	InternalAIUpdateEvent{};

	BlackBoard m_globalBB;
	std::unordered_map<std::string, BlackBoard*> m_blackBoardFind; // �� AI�� ���� ���� ������ : emplace ����
	std::list<BlackBoard> m_blackBoards;
	std::unordered_map<GameObject*, IAIComponent*> m_aiComponentMap; // GameObject�� AI ������Ʈ ����
};

static auto& AIManagers = AIManager::GetInstance();

#endif // !DYNAMICCPP_EXPORTS