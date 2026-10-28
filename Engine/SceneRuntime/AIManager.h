#include "../Utility_Framework/Foundation/EngineNodeMap.h"
#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Delegate.h"
#else
import Creator.Core.Delegate;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "IAIComponent.h"
#include "EntityHandle.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
// NodeFactory.h媛 BTHeader.h瑜??듯빐 ?꾩씠濡?怨듦툒?섎뜕 寃껊뱾?대떎(PHASE 9-8 B7?먯꽌 ?쒓굅).
// 媛곸옄 ?꾩슂??寃껋쓣 吏곸젒 諛쏅뒗??
#include "BlackBoard.h"
#include "BTBuildGraph.h"
#include <mathematics/frustum.hpp>
#include <mathematics/bounds.hpp>

class StateMachineComponent;
class BehaviorTreeComponent;
class Entity;
class Scene;

// 워커에 넘기는 것은 소유 스레드에서 봉인한 값뿐이다. 컴포넌트 주소 대신
// 등록 세대를 쓰므로 파괴·재등록·DDOL 이송 뒤의 결과는 새 객체에 적용되지 않는다.
struct AIComponentSnapshot
{
    EntityHandle m_owner;
    uint64_t m_registrationId = 0;
    int m_treeInstanceId = -1;
    uint64_t m_treeRevision = 0;
    math::aabb m_bounds;
    bool m_enabled = false;
};

struct AIUpdateSnapshot
{
    ce::dynamic_array<AIComponentSnapshot> m_components;
    std::optional<math::bounding_frustum> m_cameraFrustum;
    float m_deltaSeconds = 0.0f;
};

struct AIUpdateBatch
{
    explicit AIUpdateBatch(AIUpdateSnapshot value) : m_snapshot(std::move(value)) {}

    const AIUpdateSnapshot m_snapshot;
    // 단일 워커만 쓰고 Scene이 완료 토큰을 회수한 뒤에만 읽는다.
    ce::dynamic_array<size_t> m_visibleIndices;
};

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

	void RegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

	void UnRegisterAIComponent(Entity* gameObject, IAIComponent* aiComponent);

    // Capture/Apply는 게임 스레드 전용이고 Cull만 공용 워커에서 실행한다.
    own::shared_owner<AIUpdateBatch> CaptureAIUpdate(Scene& scene, float deltaSeconds,
        const std::optional<math::bounding_frustum>& cameraFrustum);
    static void CullAIUpdate(const AIUpdateSnapshot& snapshot, ce::dynamic_array<size_t>& visibleIndices);
    void ApplyAIUpdate(Scene& scene, const AIUpdateBatch& batch);
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

	own::shared_owner<BTBuildGraph> GetBTBuildGraphCache(const FileGuid& fileGuid)
	{
		auto it = m_btBuildGraphCache.find(fileGuid);
		if (it != m_btBuildGraphCache.end())
		{
			return it->second;
		}
		return nullptr;
	}

	void SetBTBuildGraphCache(const FileGuid& fileGuid, own::shared_owner<BTBuildGraph> graph)
	{
		m_btBuildGraphCache[fileGuid] = graph;
	}

private:

	ce::delegate<void, float>	InternalAIUpdateEvent{};

	BlackBoard m_globalBB;
    struct AIRegistration
    {
        EntityHandle m_owner;
        IAIComponent* m_component = nullptr;
        uint64_t m_registrationId = 0;
    };

    // Scene이 컴포넌트를 소유한다. 비소유 주소는 게임 스레드 레지스트리 안에만
    // 남긴다. 새 등록은 끝에 추가하고, 해지 시 남은 등록의 상대 순서를 유지한다.
    ce::dynamic_array<AIRegistration> m_aiComponentMap;
    ce::engine_node_map<uint64_t, IAIComponent*> m_aiRegistrationsById;
    uint64_t m_nextAIRegistrationId = 1;
    // 등록·해지·Capture·Apply는 게임 스레드가 직렬화한다. 이 잠금은 레지스트리
    // 조회만 보호하며 컴포넌트 수명을 워커까지 연장하는 수단이 아니다.
    mutable std::mutex m_aiComponentMutex;
	ce::engine_node_map<FileGuid, own::shared_owner<BTBuildGraph>> m_btBuildGraphCache; // BT 빌드 그래프 캐시
};

#if defined(CE_SCENE_HEADLESS)
#define AIManagers (AIManager::GetIfAlive())
#else
static auto AIManagers = AIManager::GetInstance();
#endif
