#include "AIManager.h"
#include "BehaviorTreeComponent.h"
#include "StateMachineComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "MeshRenderer.h"
//#include <execution>

BlackBoard* AIManager::CreateBlackBoard(const std::string& aiName)
{
	// 블랙보드가 이미 존재하는지 확인
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

void AIManager::InternalAIUpdate(float deltaSeconds,
	const DirectX::BoundingFrustum& cameraFrustum)
{
	std::vector<IAIComponent*> compVec{};
	Scene* activeScene = SceneManagers->GetActiveScene();
	if (!activeScene) return;

	std::vector<std::pair<EntityHandle, IAIComponent*>> snapshot;
	{
		std::scoped_lock lock(m_aiComponentMutex);
		snapshot.reserve(m_aiComponentMap.size());
		for (const auto& entry : m_aiComponentMap)
			snapshot.push_back(entry);
	}

	for (const auto& [handle, comp] : snapshot)
	{
		Entity* obj = activeScene->Resolve(handle);
		if (!obj || !comp) continue;

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

		if (cameraFrustum.Intersects(objBox))
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

	Scene* scene = gameObject->GetScene();
	const EntityHandle handle = scene ? scene->HandleOf(gameObject->m_index) : EntityHandle{};
	if (!handle.IsValid()) return;

	std::scoped_lock lock(m_aiComponentMutex);
	std::erase_if(m_aiComponentMap, [aiComponent](const auto& entry) noexcept
	{
		return entry.second == aiComponent;
	});
	m_aiComponentMap.emplace(handle, aiComponent);
}

void AIManager::UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent)
{
	if (!gameObject || !aiComponent)
		return;

	std::scoped_lock lock(m_aiComponentMutex);
	std::erase_if(m_aiComponentMap, [aiComponent](const auto& pair) noexcept
	{
		return nullptr == pair.second || pair.second == aiComponent;
	});
}

size_t AIManager::GetRegisteredAIComponentCount() const
{
	std::scoped_lock lock(m_aiComponentMutex);
	return m_aiComponentMap.size();
}

bool AIManager::IsAIComponentRegistered(const IAIComponent* aiComponent) const
{
	if (!aiComponent) return false;
	std::scoped_lock lock(m_aiComponentMutex);
	return std::ranges::any_of(m_aiComponentMap,
		[aiComponent](const auto& entry) { return entry.second == aiComponent; });
}

// AIManager::CreateNode媛 ?ш린 ?덉뿀?? PHASE 9-8 B7?먯꽌 ?쒓굅?덈떎.
// ?몃뱶 ?앹꽦? 愿由?痢?BTNodeFactory媛 ?섍퀬, ?ㅼ씠?곕툕?먮뒗 NodeFactory ?먯껜媛 ?녿떎.
// ?몄텧泥섎룄 ?대? 0?댁뿀??

void AIManager::ClearTreeInAIComponent()
{
	std::vector<std::pair<EntityHandle, IAIComponent*>> snapshot;
	{
		std::scoped_lock lock(m_aiComponentMutex);
		for (const auto& entry : m_aiComponentMap) snapshot.push_back(entry);
	}

	for (const auto& [gameObject, aiComponent] : snapshot)
	{
		if (!gameObject.IsValid() || !aiComponent)
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
	// Behavior Tree 노드 팩토리 초기화
	// ?ㅼ씠?곕툕 ?몃뱶 ?⑺넗由?珥덇린?붽? ?ш린 ?덉뿀?? PHASE 9-8 B7?먯꽌 ?쒓굅?덈떎.
	//
	// 鍮뚰듃???몃뱶(Sequence쨌Selector쨌WeightedSelector쨌Inverter)??愿由?痢≪쑝濡?
	// ?댁떇?먭퀬(B1), ?ъ슜???몃뱶???앹꽦湲곌? 留뚮뱺 ?깅줉?쒓? ?좊떎(B5). ?ㅼ씠?곕툕??
	// ?щ낯???④린硫?"?ш린???덈뒗???湲곗뿏 ?녿뒗 ?몃뱶"媛 ?앷릿??

	std::vector<std::pair<EntityHandle, IAIComponent*>> snapshot;
	{
		std::scoped_lock lock(m_aiComponentMutex);
		for (const auto& entry : m_aiComponentMap) snapshot.push_back(entry);
	}

	// 모든 AI 컴포넌트 초기화
	for (const auto& [gameObject, aiComponent] : snapshot)
	{
		if (!gameObject.IsValid() || !aiComponent)
			continue;
		//지금은 BT 컴포넌트만 초기화
		if (aiComponent->GetAIType() == AIType::BT)
		{
			BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
			ptr->GraphToBuild();
		}
	}

}

