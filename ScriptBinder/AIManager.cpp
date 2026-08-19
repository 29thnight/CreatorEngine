#ifndef DYNAMICCPP_EXPORTS
#include "AIManager.h"
#include "BehaviorTreeComponent.h"
#include "StateMachineComponent.h"
#include "SceneManager.h"
#include "MeshRenderer.h"
#include "Camera.h"
//#include <execution>

BlackBoard* AIManager::CreateBlackBoard(const std::string& aiName)
{
	// ºí·¢º¸µå°¡ ÀÌ¹Ì Á¸ÀçÇÏ´ÂÁö È®ÀÎ
	if (m_blackBoardFind.find(aiName) != m_blackBoardFind.end())
		return m_blackBoardFind[aiName];

	auto it = m_blackBoards.emplace();
	auto* bb = &(*it);
	m_blackBoardFind.emplace(aiName, bb);

	return bb;
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

void AIManager::InternalAIUpdate(float deltaSeconds)
{
	std::vector<IAIComponent*> compVec{};
	auto camera = CameraManagement->GetLastCamera();
	if (!camera) return;

	for (auto& [ptr, comp] : m_aiComponentMap)
	{
		auto obj = ptr.lock();
		if (!obj || !comp) continue;

		if (obj->m_ownerScene != SceneManagers->GetActiveScene()) continue;

		DirectX::BoundingBox objBox{};
		objBox.Extents = { 3.f, 3.f, 3.f };

		auto meshComp = obj->GetComponent<MeshRenderer>();
		if (meshComp)
		{
			objBox = meshComp->GetBoundingBox();
		}
		else
		{
			DirectX::BoundingBox localObjBox{ objBox };
			auto mat = obj->Transform_().GetWorldMatrix();
			localObjBox.Transform(objBox, mat);
		}

		auto frustum = camera->GetFrustum();
		if (frustum.Intersects(objBox))
		{
			compVec.push_back(comp);
		}
	}

	auto updateFunc = [deltaSeconds](IAIComponent* comp)
	{
		try
		{
			comp->InternalAIUpdate(deltaSeconds);
		}
		catch (const std::exception& e)
		{
			std::cerr << "InternalAIUpdate Exception : " << e.what() << std::endl;
		}
	};

	std::ranges::for_each(compVec.begin(), compVec.end(), updateFunc);
}

void AIManager::RegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	// GameObject¿Í AI ÄÄÆ÷³ÍÆ® ¸ÅÇÎ
	//m_aiComponentMap[gameObject] = aiComponent;

	m_aiComponentMap.emplace(gameObject->weak_from_this(), aiComponent);
}

void AIManager::UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	//auto it = m_aiComponentMap.find(gameObject);
	//if (it != m_aiComponentMap.end() && it->second == aiComponent)
	//{
	//	m_aiComponentMap.erase(it);
	//}

	std::erase_if(m_aiComponentMap, [gameObject, aiComponent](const auto& pair) noexcept
	{
		const auto& [weakObj, comp] = pair;

		if (weakObj.expired() || nullptr == comp)
			return true;

		auto obj = weakObj.lock();
		return (obj.get() == gameObject && comp == aiComponent);
	});
}

// AIManager::CreateNodeê°€ ?¬ê¸° ?ˆì—ˆ?? PHASE 9-8 B7?ì„œ ?œê±°?ˆë‹¤.
// ?¸ë“œ ?ì„±?€ ê´€ë¦?ì¸?BTNodeFactoryê°€ ?˜ê³ , ?¤ì´?°ë¸Œ?ëŠ” NodeFactory ?ì²´ê°€ ?†ë‹¤.
// ?¸ì¶œì²˜ë„ ?´ë? 0?´ì—ˆ??

void AIManager::ClearTreeInAIComponent()
{
	for (auto& [gameObject, aiComponent] : m_aiComponentMap)
	{
		if (gameObject.expired())
			continue;

		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->ClearTree();
		}
	}
}

void AIManager::InitalizeBehaviorTreeSystem()
{
	// Behavior Tree ³ëµå ÆÑÅä¸® ÃÊ±âÈ­
	// ?¤ì´?°ë¸Œ ?¸ë“œ ?©í† ë¦?ì´ˆê¸°?”ê? ?¬ê¸° ?ˆì—ˆ?? PHASE 9-8 B7?ì„œ ?œê±°?ˆë‹¤.
	//
	// ë¹ŒíŠ¸???¸ë“œ(SequenceÂ·SelectorÂ·WeightedSelectorÂ·Inverter)??ê´€ë¦?ì¸¡ìœ¼ë¡?
	// ?´ì‹?ê³ (B1), ?¬ìš©???¸ë“œ???ì„±ê¸°ê? ë§Œë“  ?±ë¡?œê? ? ë‹¤(B5). ?¤ì´?°ë¸Œ??
	// ?¬ë³¸???¨ê¸°ë©?"?¬ê¸°???ˆëŠ”???€ê¸°ì—” ?†ëŠ” ?¸ë“œ"ê°€ ?ê¸´??

	// m_aiComponentMap ¼øÈ¸ÇÏ¸é¼­ ¸¸·áµÈ GameObject pair Á¤¸®
	std::erase_if(m_aiComponentMap, [](const auto& pair) noexcept
	{
		const auto& [weakObj, comp] = pair;
		return weakObj.expired();
	});

	// ¸ðµç AI ÄÄÆ÷³ÍÆ® ÃÊ±âÈ­
	for (auto& [gameObject, aiComponent] : m_aiComponentMap)
	{
		if (gameObject.expired())
			continue;
		//Áö±ÝÀº BT ÄÄÆ÷³ÍÆ®¸¸ ÃÊ±âÈ­
		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->GraphToBuild();
		}
	}

}

#endif // DYNAMICCPP_EXPORTS