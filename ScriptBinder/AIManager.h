#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "IAIComponent.h"
// NodeFactory.hê°€ BTHeader.hë¥??µí•´ ?„ì´ë¡?ê³µê¸‰?˜ë˜ ê²ƒë“¤?´ë‹¤(PHASE 9-8 B7?ì„œ ?œê±°).
// ê°ì ?„ìš”??ê²ƒì„ ì§ì ‘ ë°›ëŠ”??
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

	BlackBoard* CreateBlackBoard(const std::string& aiName); // ÀÌ°Íµµ ´Ù½Ã ÀÛ¼º : ºí·¢º¸µå¿¡ ´ëÇÑ ÆÑÅä¸®°¡ ÇÊ¿äÇÑ °ÅÀÓ

	void RemoveBlackBoard(const std::string& aiName); // ÀÌ°Íµµ ´Ù½Ã ÀÛ¼º

	void RegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

	void UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

	void InternalAIUpdate(float deltaSeconds);


	void ClearTreeInAIComponent();

	void InitalizeBehaviorTreeSystem();

	// BT ?¬ìš©???¸ë“œ???´ë¦„ ëª©ë¡Â·?±ë¡ ?ì •???¬ê¸° ?ˆì—ˆ?? PHASE 9-8 B6?ì„œ ?œê±°?ˆë‹¤.
	//
	// ?¸ë“œ êµ¬í˜„??ê´€ë¦?ì¸¡ìœ¼ë¡???²¨ ê°”ìœ¼ë¯€ë¡?"ë¬´ì—‡???±ë¡?ëŠ”ê°€"??ì§„ì‹¤??ê·¸ìª½???ˆë‹¤.
	// ?¤ì´?°ë¸Œê°€ ?¬ë³¸???¤ë©´ ?˜ì´ ?´ê¸‹?????ˆê³ , ?´ê¸‹?˜ë©´ ?¸ì§‘ê¸?ëª©ë¡ê³??¤ì œ ì¡°ë¦½
	// ê²°ê³¼ê°€ ?¬ë¼??"ë©”ë‰´?ëŠ” ?ˆëŠ”??ë¶™ì´ë©????„ëŠ” ?¸ë“œ"ê°€ ?œë‹¤.
	// ?¸ì§‘ê¸°ëŠ” ClrHost::GetBTNodeTypeNames / HasBTNodeType??ì§ì ‘ ?´ë‹¤.

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
	std::unordered_map<std::string, BlackBoard*> m_blackBoardFind; // °¢ AI¿¡ ´ëÇÑ °³º° ºí·¢º¸µå : emplace Àü¿ë
	plf::colony<BlackBoard> m_blackBoards;
	plf::colony<std::pair<std::weak_ptr<Entity>, IAIComponent*>> m_aiComponentMap; // GameObject¿Í AI ÄÄÆ÷³ÍÆ® ¸ÅÇÎ
	std::unordered_map<FileGuid, std::shared_ptr<BTBuildGraph>> m_btBuildGraphCache; // BT ºôµå ±×·¡ÇÁ Ä³½Ã
};

static auto AIManagers = AIManager::GetInstance();

#endif // !DYNAMICCPP_EXPORTS