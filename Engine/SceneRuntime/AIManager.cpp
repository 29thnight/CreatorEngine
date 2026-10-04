#include "AIManager.h"
#include "BehaviorTreeComponent.h"
#include "StateMachineComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "Mathematics.Intersect.h"
#include <mathematics/transform.hpp>
#include <stdexcept>
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

std::shared_ptr<AIUpdateBatch> AIManager::CaptureAIUpdate(Scene& scene, float deltaSeconds,
    const std::optional<math::bounding_frustum>& cameraFrustum)
{
    std::vector<AIRegistration> registrations;
    {
        std::scoped_lock lock(m_aiComponentMutex);
        registrations.reserve(m_aiComponentMap.size());
        for (const auto& entry : m_aiComponentMap)
        {
            registrations.push_back(entry);
        }
    }

    AIUpdateSnapshot snapshot;
    snapshot.m_deltaSeconds = deltaSeconds;
    snapshot.m_cameraFrustum = cameraFrustum;
    snapshot.m_components.reserve(registrations.size());
    for (const auto& entry : registrations)
    {
        Entity* owner = scene.Resolve(entry.m_owner);
        if (!owner || !entry.m_component || owner->IsDestroyMark())
        {
            continue;
        }

        auto* component = dynamic_cast<Component*>(entry.m_component);
        if (!component || component->GetOwner() != owner || component->IsDestroyMark())
        {
            continue;
        }

        AIComponentSnapshot captured;
        captured.m_owner = entry.m_owner;
        captured.m_registrationId = entry.m_registrationId;
        captured.m_enabled = owner->IsEnabled() && component->IsEnabled() && SceneManagers->IsGameStart();
        if (auto* tree = dynamic_cast<BehaviorTreeComponent*>(entry.m_component))
        {
            captured.m_treeInstanceId = tree->GetTreeInstanceID();
            captured.m_treeRevision = tree->GetTreeRevision();
            captured.m_enabled = captured.m_enabled && captured.m_treeInstanceId >= 0;
        }

        // 행렬과 메시 bounds는 다음 프레임에 바뀔 수 있으므로 이 자리에서만 읽는다.
        captured.m_bounds = math::aabb{math::vector3{}, math::vector3{3.f, 3.f, 3.f}};
        if (auto* mesh = owner->GetComponent<MeshRenderer>())
        {
            captured.m_bounds = mesh->GetBoundingBox();
        }
        else
        {
            captured.m_bounds = math::transform(captured.m_bounds, owner->Transform_().GetWorldMatrix());
        }
        snapshot.m_components.push_back(captured);
    }

    auto batch = std::make_shared<AIUpdateBatch>(std::move(snapshot));
    batch->m_visibleIndices.reserve(batch->m_snapshot.m_components.size());
    return batch;
}

void AIManager::CullAIUpdate(const AIUpdateSnapshot& snapshot, std::vector<size_t>& visibleIndices)
{
    // Scene/Entity/Component/CLR에 접근하지 않는다. 출력 순서도 캡처 순서 그대로다.
    for (size_t index = 0; index < snapshot.m_components.size(); ++index)
    {
        const auto& component = snapshot.m_components[index];
        if (component.m_enabled && (component.m_bounds.is_empty() || !snapshot.m_cameraFrustum
            || math::intersects(*snapshot.m_cameraFrustum, component.m_bounds)))
        {
            visibleIndices.push_back(index);
        }
    }
}

void AIManager::ApplyAIUpdate(Scene& scene, const AIUpdateBatch& batch)
{
    if (SceneManagers->GetActiveScene() != &scene || !SceneManagers->IsPlayCommitted()
        || !SceneManagers->IsGameStart() || SceneManagers->IsGamePaused()
        || SceneManagers->HasPendingSceneStructureChange() || SceneManagers->IsDecommissioning())
    {
        return;
    }

    for (const size_t index : batch.m_visibleIndices)
    {
        const auto& captured = batch.m_snapshot.m_components[index];
        Entity* owner = scene.Resolve(captured.m_owner);
        if (!owner || owner->IsDestroyMark() || !owner->IsEnabled())
        {
            continue;
        }

        IAIComponent* aiComponent = nullptr;
        {
            std::scoped_lock lock(m_aiComponentMutex);
            const auto found = m_aiRegistrationsById.find(captured.m_registrationId);
            if (found != m_aiRegistrationsById.end())
            {
                aiComponent = found->second;
            }
        }
        // 재등록 시에는 주소가 같아도 새 세대다. 등록 해지 훅이 파괴보다 먼저
        // 실행되므로, 현재 세대 조회가 성공한 뒤에만 살아 있는 컴포넌트를 읽는다.
        if (!aiComponent)
        {
            continue;
        }
        auto* component = dynamic_cast<Component*>(aiComponent);
        if (!component || component->GetOwner() != owner || component->IsDestroyMark() || !component->IsEnabled())
        {
            continue;
        }
        if (auto* tree = dynamic_cast<BehaviorTreeComponent*>(aiComponent);
            tree && (tree->GetTreeInstanceID() != captured.m_treeInstanceId
                || tree->GetTreeRevision() != captured.m_treeRevision))
        {
            continue;
        }

        try
        {
            // 기존 큐만 채운다. 관리 BT 실행과 일괄 crossing은 FlushAITicks에 남긴다.
            aiComponent->InternalAIUpdate(batch.m_snapshot.m_deltaSeconds);
        }
        catch (const std::exception& exception)
        {
            std::cerr << "InternalAIUpdate Exception : " << exception.what() << std::endl;
        }
    }
}

void AIManager::RegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent)
{
    if (!gameObject || !aiComponent)
    {
        return;
    }

    Scene* scene = gameObject->GetScene();
    const EntityHandle handle = scene ? scene->HandleOf(gameObject->m_index) : EntityHandle{};
    if (!handle.IsValid())
    {
        return;
    }

    std::scoped_lock lock(m_aiComponentMutex);
    // 0은 무효다. 64비트 세대를 모두 쓴 경우에도 옛 결과의 identity를 재사용하지 않는다.
    if (m_nextAIRegistrationId == 0)
    {
        throw std::overflow_error("AI registration identity exhausted");
    }
    std::erase_if(m_aiComponentMap, [this, aiComponent](const AIRegistration& entry) noexcept
    {
        if (entry.m_component != aiComponent)
        {
            return false;
        }
        m_aiRegistrationsById.erase(entry.m_registrationId);
        return true;
    });
    const uint64_t registrationId = m_nextAIRegistrationId++;
    auto registration = m_aiComponentMap.emplace(AIRegistration{handle, aiComponent, registrationId});
    try
    {
        m_aiRegistrationsById.emplace(registrationId, aiComponent);
    }
    catch (...)
    {
        m_aiComponentMap.erase(registration);
        throw;
    }
}

void AIManager::UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent)
{
    if (!gameObject || !aiComponent)
    {
        return;
    }

    std::scoped_lock lock(m_aiComponentMutex);
    std::erase_if(m_aiComponentMap, [this, aiComponent](const AIRegistration& entry) noexcept
    {
        if (entry.m_component && entry.m_component != aiComponent)
        {
            return false;
        }
        m_aiRegistrationsById.erase(entry.m_registrationId);
        return true;
    });
}

size_t AIManager::GetRegisteredAIComponentCount() const
{
    std::scoped_lock lock(m_aiComponentMutex);
    return m_aiComponentMap.size();
}

bool AIManager::IsAIComponentRegistered(const IAIComponent* aiComponent) const
{
    if (!aiComponent)
    {
        return false;
    }
    std::scoped_lock lock(m_aiComponentMutex);
    return std::ranges::any_of(m_aiComponentMap,
        [aiComponent](const AIRegistration& entry) { return entry.m_component == aiComponent; });
}

// AIManager::CreateNode媛 ?ш린 ?덉뿀?? PHASE 9-8 B7?먯꽌 ?쒓굅?덈떎.
// ?몃뱶 ?앹꽦? 愿由?痢?BTNodeFactory媛 ?섍퀬, ?ㅼ씠?곕툕?먮뒗 NodeFactory ?먯껜媛 ?녿떎.
// ?몄텧泥섎룄 ?대? 0?댁뿀??

void AIManager::ClearTreeInAIComponent()
{
    std::vector<AIRegistration> snapshot;
    {
        std::scoped_lock lock(m_aiComponentMutex);
        for (const auto& entry : m_aiComponentMap)
        {
            snapshot.push_back(entry);
        }
    }

    for (const auto& entry : snapshot)
    {
        auto* aiComponent = entry.m_component;
        if (!entry.m_owner.IsValid() || !aiComponent)
        {
            continue;
        }

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

    std::vector<AIRegistration> snapshot;
    {
        std::scoped_lock lock(m_aiComponentMutex);
        for (const auto& entry : m_aiComponentMap)
        {
            snapshot.push_back(entry);
        }
    }

    // 모든 AI 컴포넌트 초기화
    for (const auto& entry : snapshot)
    {
        auto* aiComponent = entry.m_component;
        if (!entry.m_owner.IsValid() || !aiComponent)
        {
            continue;
        }
        //지금은 BT 컴포넌트만 초기화
        if (aiComponent->GetAIType() == AIType::BT)
        {
            BehaviorTreeComponent* ptr = static_cast<BehaviorTreeComponent*>(aiComponent);
            ptr->GraphToBuild();
        }
    }

}

