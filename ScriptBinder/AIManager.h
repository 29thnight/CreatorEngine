#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "IAIComponent.h"
// NodeFactory.h가 BTHeader.h를 통해 전이로 공급하던 것들이다(PHASE 9-8 B7에서 제거).
// 각자 필요한 것을 직접 받는다.
#include "Blackboard.h"
#include "BTBuildGraph.h"

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

	BlackBoard* CreateBlackBoard(const std::string& aiName); // �̰͵� �ٽ� �ۼ� : �������忡 ���� ���丮�� �ʿ��� ����

	void RemoveBlackBoard(const std::string& aiName); // �̰͵� �ٽ� �ۼ�

	void RegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);

	void UnRegisterAIComponent(GameObject* gameObject, IAIComponent* aiComponent);

	void InternalAIUpdate(float deltaSeconds);


	void ClearTreeInAIComponent();

	void InitalizeBehaviorTreeSystem();

	// BT 사용자 노드의 이름 목록·등록 판정이 여기 있었다. PHASE 9-8 B6에서 제거했다.
	//
	// 노드 구현이 관리 측으로 옮겨 갔으므로 "무엇이 등록됐는가"의 진실도 그쪽에 있다.
	// 네이티브가 사본을 들면 둘이 어긋날 수 있고, 어긋나면 편집기 목록과 실제 조립
	// 결과가 달라져 "메뉴에는 있는데 붙이면 안 도는 노드"가 된다.
	// 편집기는 ClrHost::GetBTNodeTypeNames / HasBTNodeType을 직접 쓴다.

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

	Core::Delegate<void, float>	InternalAIUpdateEvent{};

	BlackBoard m_globalBB;
	std::unordered_map<std::string, BlackBoard*> m_blackBoardFind; // �� AI�� ���� ���� �������� : emplace ����
	plf::colony<BlackBoard> m_blackBoards;
	plf::colony<std::pair<std::weak_ptr<GameObject>, IAIComponent*>> m_aiComponentMap; // GameObject�� AI ������Ʈ ����
	std::unordered_map<FileGuid, std::shared_ptr<BTBuildGraph>> m_btBuildGraphCache; // BT ���� �׷��� ĳ��
};

static auto& AIManagers = AIManager::GetInstance();

#endif // !DYNAMICCPP_EXPORTS