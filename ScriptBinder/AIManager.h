#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "HotLoadSystem.h"
#include "NodeFactory.h"
#include "IAIComponent.h"

class StateMachineComponent;
class BehaviorTreeComponent;
class GameObject;
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

	BlackBoard* CreateBlackBoard(const std::string& aiName); // 이것도 다시 작성 : 블랙보드에 대한 팩토리가 필요한 거임

	void RemoveBlackBoard(const std::string& aiName); // 이것도 다시 작성

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

	std::shared_ptr<BTBuildGraph> GetBTBuildGraphCache(const FileGuid& fileGuid)
	{
		auto it = m_btBuildGraphCache.find(fileGuid);
		if (it != m_btBuildGraphCache.end())
		{
			return it->second;
		}
		return nullptr;
	}

	void SetBTBuildGraphCache(const FileGuid& fileGuid, std::shared_ptr<BTBuildGraph> graph)
	{
		m_btBuildGraphCache[fileGuid] = graph;
	}

private:
	friend class HotLoadSystem;

	std::vector<std::string> m_btActionNodeNames{};
	std::vector<std::string> m_btConditionNodeNames{};
	std::vector<std::string> m_btConditionDecoratorNodeNames{};
	Core::Delegate<void, float>	InternalAIUpdateEvent{};

	BlackBoard m_globalBB;
	std::unordered_map<std::string, BlackBoard*> m_blackBoardFind; // 각 AI에 대한 개별 블랙보드 : emplace 전용
	plf::colony<BlackBoard> m_blackBoards;
	plf::colony<std::pair<std::weak_ptr<GameObject>, IAIComponent*>> m_aiComponentMap; // GameObject와 AI 컴포넌트 매핑
	std::unordered_map<FileGuid, std::shared_ptr<BTBuildGraph>> m_btBuildGraphCache; // BT 빌드 그래프 캐시
};

static auto& AIManagers = AIManager::GetInstance();

#endif // !DYNAMICCPP_EXPORTS