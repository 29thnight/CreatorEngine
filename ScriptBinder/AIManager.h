#pragma once
#include "Core.Minimal.h"
#include "IAIComponent.h"
#include "EntityHandle.h"
#include <mutex>
// NodeFactory.h媛 BTHeader.h瑜??듯빐 ?꾩씠濡?怨듦툒?섎뜕 寃껊뱾?대떎(PHASE 9-8 B7?먯꽌 ?쒓굅).
// 媛곸옄 ?꾩슂??寃껋쓣 吏곸젒 諛쏅뒗??
#include "BlackBoard.h"
#include "BTBuildGraph.h"

class StateMachineComponent;
class BehaviorTreeComponent;
class Entity;
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

	void RegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

	void UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

	void InternalAIUpdate(float deltaSeconds);
	size_t GetRegisteredAIComponentCount() const;
	bool IsAIComponentRegistered(const IAIComponent* aiComponent) const;


	void ClearTreeInAIComponent();

	void InitalizeBehaviorTreeSystem();

	// BT ?ъ슜???몃뱶???대쫫 紐⑸줉쨌?깅줉 ?먯젙???ш린 ?덉뿀?? PHASE 9-8 B6?먯꽌 ?쒓굅?덈떎.
	//
	// ?몃뱶 援ы쁽??愿由?痢≪쑝濡???꺼 媛붿쑝誘濡?"臾댁뾿???깅줉?먮뒗媛"??吏꾩떎??洹몄そ???덈떎.
	// ?ㅼ씠?곕툕媛 ?щ낯???ㅻ㈃ ?섏씠 ?닿툔?????덇퀬, ?닿툔?섎㈃ ?몄쭛湲?紐⑸줉怨??ㅼ젣 議곕┰
	// 寃곌낵媛 ?щ씪??"硫붾돱?먮뒗 ?덈뒗??遺숈씠硫????꾨뒗 ?몃뱶"媛 ?쒕떎.
	// ?몄쭛湲곕뒗 ClrHost::GetBTNodeTypeNames / HasBTNodeType??吏곸젒 ?대떎.

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
	std::unordered_map<std::string, BlackBoard*> m_blackBoardFind; // 각 AI에 대한 개별 블랙보드 : emplace 전용
	plf::colony<BlackBoard> m_blackBoards;
	// Scene이 유일한 소유자다. 레지스트리는 sceneId까지 포함한 세대 검증 핸들과
	// 비소유 컴포넌트 포인터만 보관한다. DDOL 이송은 씬 편입/이탈 훅에서
	// 옛 핸들을 제거하고 새 씬 핸들로 다시 등록한다.
	plf::colony<std::pair<EntityHandle, IAIComponent*>> m_aiComponentMap;
	// 업데이트는 Scene::m_AIFuture에서, 등록/해지는 게임 스레드에서 일어난다.
	// 컨테이너는 잠금 아래 snapshot만 만들고 실제 AI 코드는 잠금 밖에서 실행한다.
	mutable std::mutex m_aiComponentMutex;
	std::unordered_map<FileGuid, std::shared_ptr<BTBuildGraph>> m_btBuildGraphCache; // BT 빌드 그래프 캐시
};

static auto AIManagers = AIManager::GetInstance();

