#include "SceneManager.h"
#include "RenderScene.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "FoliageComponent.h"
#include "Material.h"
#include "Object.h"
#include "Transform.h" // 레인 2: Entity::GetComponent<Transform>() 직접 참조
#include "BoneComponent.h" // E7-b: 뼈 구파일 승격(Entity::AddComponent<BoneComponent>())
// 프리팹 재연결은 이 헤더로 한다. PrefabEditor는 저작 도구라 Editor 소유이고
// (E3-4에서 EngineEntry로 옮겼다) Core는 Editor를 물지 않는다 — Prefab.cpp도
// 같은 이유로 이 헤더를 쓴다(SceneGraphRedesignPlan P2).
//
// 여기 있던 "PrefabEditor.h는 DYNAMICCPP_EXPORTS로 가드돼 있어 못 쓴다"는 설명은
// 두 번 낡아 있었다: C++ 핫리로드가 은퇴해 그 매크로를 정의하는 곳이 하나도 없어
// 가드가 무력했고(솔루션에 Dynamic_CPP 프로젝트 자체가 없다), 이제는 층이 갈렸다.
#include "PrefabUtility.h"
#include "DataSystem.h"
#include "ComponentFactory.h"
#include "AuthoringDocumentAccess.h"
#include "EntityAuthoringRead.h" // D3-a-2: 저작 읽기 어댑터
#include "RegisterReflectManual.h" // CT4: 명시 메타 이전 타입의 등록 (def 스캔 밖)
#include "Profiler.h"
#include "SerializationProfiler.h" // D0: 직렬화 기준선 계측
#include "InputActionManager.h"
#include "TagManager.h"
#include "ReflectionRegister.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "Component.h"
#include "TimeSystem.h"
#include "GpuDiagnostics.h"
#include "ScriptComponent.h"
#include "ClrHost.h"

// 예전에는 여기에 SuspendSceneScripts가 있었다. 재생 시작이 에디터 씬을 복제해
// PlayScene을 만들던 시절, 원본과 사본의 스크립트 인스턴스가 둘 다 틱을 받아
// 로직이 두 벌 도는 것을 막는 봉합이었다. 씬을 복제하지 않게 되면서 두 벌이
// 생길 여지 자체가 사라져 걷어냈다.

namespace
{
    // Scene 공개/리플렉션 API는 Entity 기준 이름(m_Entities)으로 저장한다.
    // 다만 기존 .creator 자산은 m_SceneObjects 키를 갖고 있으므로 읽을 때만
    // 구 키를 별칭으로 허용한다. 새 저장 결과는 항상 m_Entities 하나로 수렴한다.
    MetaYml::Node SerializedEntities(const MetaYml::Node& sceneNode)
    {
        if (!sceneNode) return {};
        if (MetaYml::Node entities = sceneNode["m_Entities"])
            return entities;
        return sceneNode["m_SceneObjects"];
    }

    // 프리팹 인스턴스 재연결 (SceneGraphRedesignPlan §4 트랙 P, P2).
    //
    // 반드시 RemapLoadBatchIndices 이후에 불러야 한다 — 그 전에는 obj->m_parentIndex가
    // 아직 파일 인덱스 스킴이라(RemapLoadBatchIndices 선언부 주석 참고) 부모를 슬롯
    // 인덱스로 찾을 수 없다. 옛 주석 처리 블록(P-a, DesirealizeGameObject 안에 있던
    // `//PrefabUtilitys->LoadPrefabGuid(...)`)처럼 오브젝트를 하나씩 역직렬화하며
    // 처리하지 않고, 로드 배치가 끝난 뒤 한 번에 훑는 이유이기도 하다.
    //
    // 인스턴스 "루트" 판정은 옛 parent==0 관습(Prefab::InstantiateRecursive가 최초
    // 호출에만 넘기던 인자값에 우연히 기대던 것 — 인덱스 0이 "부모 없음"이 아니라
    // 그 자체로 하나의 실제 슬롯이라는 사실과 부딪힌다)을 되살리지 않는다. 대신
    // P1이 만든 명시 데이터로 한다: 부모가 없거나 부모의 m_prefabFileGuid가 나와
    // 다르면 내가 그 프리팹 인스턴스의 루트다 — 같은 프리팹 안의 자식은 부모와
    // guid가 같다(Prefab::InstantiateRecursive가 root/children 전원에 같은 guid를
    // 매기는 것과 대칭). 인스턴스 루트를 나중에 다른 오브젝트 밑으로 재부모화해도
    // 이 판정은 무너지지 않는다.
    void ReconnectPrefabInstance(Scene* scene, Entity* obj)
    {
        if (!scene || !obj || obj->m_prefabFileGuid == nullFileGuid)
            return;

        Prefab* prefab = PrefabUtilitys->LoadPrefabGuid(obj->m_prefabFileGuid);
        if (!prefab)
        {
            // 프리팹 파일이 삭제됐거나 GUID가 끊겼다 — 연결 없이 오브젝트는 그대로 살려 둔다.
            Debug->LogWarning("프리팹 재연결 실패(파일을 찾을 수 없음): " + obj->GetHashedName().ToString());
            return;
        }

        obj->m_prefab = prefab;

        bool isInstanceRoot = true;
        const Entity::Index parentIndex = obj->GetParentIndex();
        if (parentIndex != Entity::INVALID_INDEX)
        {
            if (auto parentObj = scene->TryGetEntity(parentIndex))
            {
                isInstanceRoot = (parentObj->m_prefabFileGuid != obj->m_prefabFileGuid);
            }
        }

        if (isInstanceRoot)
        {
            PrefabUtilitys->RegisterInstance(obj, prefab);
        }
    }
}

// 구파일 승격 공유 헬퍼(레인 2, SceneGraphRedesignPlan §5 예외 4).
//
// 레인 1이 Transform을 Component 파생으로 승격하면서 GameObject
// 스키마에서 m_transform 필드가 빠진다. 역직렬화기는 모르는 키를 조용히
// 무시하므로(ReflectionTypedYml.h ReadMember) 구파일(현재 저작 자산
// 218개 전부 — 씬 12·프리팹 206)의 m_transform 노드를 방치하면
// 위치·회전·크기가 에러 없이 사라진다. GameObject를 역직렬화하는 5개
// 호출부 중 실제로 파일에서 읽어 들이는 4곳(SceneManager.cpp 3곳·
// Prefab.cpp 1곳)이 이 함수를 부른다. Object.cpp의 Instantiate 클론
// 경로는 살아있는 오브젝트를 그 자리에서 Meta::Serialize로 재직렬화한
// 인메모리 노드를 읽으므로 이미 새 스키마다 — m_transform 키가 나올 수
// 없어 승격 대상이 아니다(Object.cpp 주석 참고).
//
// 헤더를 새로 두지 않고(배정 파일 밖 편집 금지 — SceneManager.h는
// 레인 2 배정 밖이다) 외부 링키지 자유 함수로 노출한다. Prefab.cpp는
// 이 선언을 자기 파일에서 forward-declare 해서 쓴다. 통합 시 정식
// 헤더 선언으로 옮기는 편이 낫다(최종 보고 참고).
namespace LegacyTransformPromotion
{
    // 뼈 구파일 승격(트랙 E, E7-b) — PromoteLegacyTransform과 같은 이름공간에
    // 두고 그 안에서 호출한다(아래). 별도 헤더 선언·별도 호출부를 늘리지
    // 않은 이유: Prefab.cpp는 PromoteLegacyTransform 하나만 forward-declare
    // 해서 쓰고(그쪽 파일은 이 슬라이스의 배정 밖이다), 이 함수를
    // PromoteLegacyTransform 내부에서 위임 호출하면 Prefab.cpp를 전혀
    // 건드리지 않고도 프리팹 인스턴스화 경로(Prefab.cpp:161)까지 자동으로
    // 덮는다. 형제 함수로 나란히 두고 4곳 모두에서 따로 불렀다면 그중
    // Prefab.cpp 1곳은 배정 밖 편집이 되어 배선 지시로 남겨야 했을 것이다.
    //
	// "구파일 여부" 판정은 PromoteLegacyTransform(m_transform 키 유무)과
	// 다르다. E7-c 이후 신파일에는 m_gameObjectType이 없고 BoneComponent가
	// 정본이다. 옛 Bone 키가 있으면서 m_components에 마커가 없는 경우만 승격한다:
    //   - 있다(신파일, 이 슬라이스 이후 재저장분) → 여기서는 아무 것도 하지
    //     않는다. 아래 m_components 로드 루프(SceneManager.cpp 4곳·
    //     Prefab.cpp 1곳, 이 함수 호출부 바로 다음)가 정상적으로 채운다.
    //     여기서 먼저 붙이면 Entity::AddComponent(Meta::Type&)의 중복
    //     검사가 오브젝트마다 "이미 존재" 경고를 찍는다(Entity.cpp:196) —
    //     흔한 정상 경로에 경고 로그가 쌓이는 것을 막는다.
	//   - 없다(구파일) → 여기서 붙여야 한다. 안 그러면
    //     Scene::UpdateModelRecursive의 Bone 분기가 HasComponent<BoneComponent>()로
    //     판정하는 순간 이 오브젝트를 건너뛰어 애니메이션이 멈춘다.
    void PromoteLegacyBone(Entity* obj, const Authoring::ReadNode& node)
    {
		// E7-c: 저장 타입은 더 이상 Entity 상태가 아니다. 옛 파일에 남은 키를
		// 이 승격 순간에만 읽고, 신형 파일은 BoneComponent 블록 자체가 정본이다.
		if (!obj || !node["m_gameObjectType"]
			|| GameObjectType::Bone != static_cast<GameObjectType>(node["m_gameObjectType"].As<int>()))
            return;

        if (const Authoring::ReadNode componentsNode = node["m_components"])
        {
            for (const auto componentNode : componentsNode)
            {
                try
                {
                    const Meta::Type* componentType = Meta::ExtractTypeFromYAML(componentNode);
                    if (componentType && componentType->typeID == type_guid(BoneComponent))
                        return; // 신파일 — 아래 m_components 로드 루프가 채운다.
                }
                catch (const std::exception&)
                {
                    // 이 항목 파싱이 실패해도 여기서는 조용히 다음 항목을 본다 —
                    // 실제 로드(m_components 루프)가 같은 노드를 다시 만나
                    // 필요하면 그때 로그를 남긴다(SceneManager.cpp 위 catch 블록).
                    continue;
                }
            }
        }

        obj->AddComponent<BoneComponent>();
    }

    // obj->GetComponent<Transform>() 접근을 가정한다 — 레인 1의 최종
    // API가 다르면 통합 담당이 이 한 줄만 맞추면 된다.
    void PromoteLegacyTransform(Entity* obj, const Authoring::ReadNode& node)
    {
        if (!obj)
            return;

        // E7-b: 뼈 승격은 아래 m_transform 유무 판정과 무관하게 항상 시도한다
        // — 신형 Transform으로 이미 재저장된 씬이라도(m_transform 키 없음)
        // 뼈 마커는 그 판정과 독립으로 붙어야 한다(위 PromoteLegacyBone 주석).
        PromoteLegacyBone(obj, node);

        const Authoring::ReadNode legacyTransformNode = node["m_transform"];
        if (!legacyTransformNode)
            return; // 신파일 — 이미 m_components 블록에서 읽혔다.

        Transform* transform = obj->GetComponent<Transform>();
        if (!transform)
        {
			// S3: UI는 Transform을 갖지 않는다 — 구파일에 m_transform 키가
            // 남아 있어도 승격할 대상이 없고, 승격해서도 안 된다(rect가 정본이다).
            // 정상 경로이므로 조용히 넘긴다. 반대로 비-UI 오브젝트에서 여기 걸리면
            // 자동 부착이 깨진 것인데, 그 유실은 verify-transform-roundtrip.ps1이
            // 값 단위로 잡는다(그 검사를 이 슬라이스 착수 전에 먼저 세운 이유다).
            return;
        }

        // position/rotation/scale만 승격한다. m_parentID는 여기서
        // 건드리지 않는다 — Transform.h 주석(97-102줄)에 따르면
        // Entity::SetParentIndex를 통해서만 바뀌어야 하는 값이고,
        // 이 함수의 모든 호출부는 obj를 만들 때 이미 itNode/node의
        // m_parentIndex로 부모를 확정한 뒤다. 게다가 Transform::SetParentID는
        // private(friend GameObject만)라 여기서는 애초에 호출할 수 없다.
        if (const auto positionNode = legacyTransformNode["position"])
            Meta::Typed::ReadScalar(positionNode, transform->position);
        if (const auto rotationNode = legacyTransformNode["rotation"])
            Meta::Typed::ReadScalar(rotationNode, transform->rotation);
        if (const auto scaleNode = legacyTransformNode["scale"])
            Meta::Typed::ReadScalar(scaleNode, transform->scale);
    }

    // ★ 전환기 오버로드 — Prefab의 read-write 소환 경로용(D3-b-3에서 사라진다).
    void PromoteLegacyTransform(Entity* obj, const MetaYml::Node& node)
    {
        PromoteLegacyTransform(obj, Authoring::ReadNode{ node });
    }
}

SceneManager::~SceneManager() = default;

void SceneManager::SetGameStart(bool isStart)
{
    if (!isStart)
    {
        SetGamePaused(false);
    }

    m_isGameStart = isStart;

    // 재생 중에는 gen2 블로킹 수집을 억제한다(PHASE 9-6).
    //
    // 편집 중과 재생 중은 원하는 것이 반대다. 편집 중에는 메모리를 제때 돌려받는 편이
    // 낫고(에셋을 계속 갈아 끼운다), 재생 중에는 프레임 예산이 우선이다 — 블로킹
    // 수집 한 번이 프레임을 통째로 삼키면 그게 곧 히칭이다.
    //
    // 보장이 아니라 요청이라는 점은 알고 쓴다. 메모리 압박이 크면 런타임이 무시하고
    // 수집한다. 그래서 이것만으로 히칭이 사라진다고 기대하지 않고, 9-7의 계측으로
    // 실제 gen2 횟수가 줄었는지 확인한 뒤에 판단한다.
    ClrHost::Get().SetManagedLatencyMode(isStart);
}

void SceneManager::SetGamePaused(bool isPaused)
{
    if (!m_isGameStart && isPaused)
    {
        return;
    }

    const bool previousState = m_isGamePaused.exchange(isPaused);
    if (previousState == isPaused)
    {
        return;
    }

    Time->ResetElapsedTime();
}

void SceneManager::ToggleGamePaused()
{
    SetGamePaused(!IsGamePaused());
}

void SceneManager::ManagerInitialize()
{
    RegisterReflectManual(); // CT4: 명시 메타 파일럿 4타입 — def에서 빠진 몫
    ComponentFactorys->Initialize();
	// 공용 작업자 풀. 소유는 층 1로 내렸고(WorkerPool.h) 수명만 여기서 잡는다.
	WorkerPools->Startup();
    m_inputActionManager = new InputActionManager();
    InputActionManagers = m_inputActionManager;
    InputActionManagers->LoadManager();
}

bool SceneManager::HasPendingSceneStructureChange() const
{
    const bool needsPlayScene    = m_isGameStart && !m_isEditorSceneLoaded;
    const bool needsEditorScene  = !m_isGameStart && m_isEditorSceneLoaded;
    const bool needsActivation   = m_sceneToActivate.load() != nullptr;
    return needsPlayScene || needsEditorScene || needsActivation;
}

void SceneManager::ApplyPendingSceneStructureChange()
{
    // 호출 지점이 렌더 정지 구간임을 전제로 한다(선언부 주석 참고).

    // 씬 교체도 같은 이유로 여기서 처리한다. 활성 씬을 갈아끼우고 이전 씬을
    // 파괴하는 작업이라 렌더가 도는 중에 하면 안 된다.
    if (m_sceneToActivate.load())
    {
        BeforeAwakeSceneLoad();
    }

    if (m_isGameStart && !m_isEditorSceneLoaded)
    {
        auto activeScenePtr = m_activeScene.load();
        if (!activeScenePtr) return;
        PROFILE_CPU_BEGIN("BeginPlayTransaction");
        BeginPlayTransaction();
        PROFILE_CPU_END();
        PROFILE_CPU_BEGIN("Reset");
        activeScenePtr->Reset();
        PROFILE_CPU_END();
		m_isEditorSceneLoaded = true;
    }
    else if (!m_isGameStart && m_isEditorSceneLoaded)
    {
        PROFILE_CPU_BEGIN("EndPlayTransaction");
        EndPlayTransaction();
        PROFILE_CPU_END();
    }
}

void SceneManager::Editor()
{
    PROFILE_CPU_BEGIN("Editor");

    // 재생/정지 전환은 여기서 하지 않는다. 렌더 스레드가 도는 중이기 때문이다.
    // Dx11Main이 렌더 배리어 사이에서 ApplyPendingSceneStructureChange를 부른다.

    if (!m_isGameStart)
    {
        auto activeScenePtr = m_activeScene.load();
        if (!activeScenePtr) return;
        // Sweep DDOL bucket for destroyed objects
		std::erase_if(m_dontDestroyOnLoadObjects, [](Object* o){ return !o || o->IsDestroyMark(); });
		//m_inputActionManager->ClearActionMaps();  //&&&&&TODO:게임스타트 한번만 초기화하고 다시들어가게
        m_isInitialized = false; // Reset initialization state for editor scene
        activeScenePtr->DrainPendingLifecycle();
	}
    PROFILE_CPU_END();
}

void SceneManager::Initialization()
{
    if(!m_isInitialized)
    {
		m_isInitialized = true;
    }

    if (m_loadingSceneFuture.valid() &&
        m_loadingSceneFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            // .get() retrieves the result. It will re-throw any exception caught in the async task.
            Scene* loadedScene = m_loadingSceneFuture.get();
            if (loadedScene)
            {
                // The scene is loaded, now activate it on the main thread.
                ActivateScene(loadedScene);
            }
        }
        catch (const std::exception& e)
        {
            Debug->LogError("Failed to activate loaded scene.");
            // Handle loading failure
        }
        // The future is now invalid after .get(), so this block won't run again until a new scene is loaded.
    }

    if (!m_activeScene) return;

    // 씬 교체(BeforeAwakeSceneLoad)는 여기서 하지 않는다.
    // Dx11Main이 렌더 배리어 사이에서 처리하므로, 여기서는 교체가 끝난 씬을 깨우기만 한다.

    // 옛 Awake→OnEnable→Start 3단은 뒤의 둘이 빈 함수라 사실상 드레인 하나였다
    // (트랙 C · C4). 활성 전이는 Component::SetEnabled가 그 자리에서 처리하고,
    // Start는 이 드레인이 pendingStart까지 소진한다.
    PROFILE_CPU_BEGIN("DrainPendingLifecycle");
	m_activeScene.load()->DrainPendingLifecycle();
    PROFILE_CPU_END();
}

void SceneManager::Physics(float deltaSecond)
{
    if (!m_activeScene) return;
    PROFILE_CPU_BEGIN("FixedUpdate");
    m_activeScene.load()->FixedUpdate(deltaSecond);
    PROFILE_CPU_END();
}

void SceneManager::InputEvents(float deltaSecond)
{
    PROFILE_CPU_BEGIN("InputEvents");
    InputEvent.Broadcast(deltaSecond);
    PROFILE_CPU_END();
}

void SceneManager::GameLogic(float deltaSecond)
{
    if (!m_activeScene) return;

    PROFILE_CPU_BEGIN("Update");
    m_activeScene.load()->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("YieldNull");
    m_activeScene.load()->YieldNull();
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("InternalAnimationUpdateEvent");
    InternalAnimationUpdateEvent.Broadcast(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("LateUpdate");
    m_activeScene.load()->LateUpdate(deltaSecond);
    PROFILE_CPU_END();
}

void SceneManager::SceneRendering(float deltaSecond)
{
    SceneRenderingEvent.Broadcast(deltaSecond);
}

void SceneManager::OnDrawGizmos()
{
	OnDrawGizmosEvent.Broadcast();
}

void SceneManager::GUIRendering()
{
    GUIRenderingEvent.Broadcast();
}

void SceneManager::EndOfFrame()
{
    PROFILE_CPU_BEGIN("EndOfFrame");
	CoroutineManagers->yield_WaitForEndOfFrame();
    endOfFrameEvent.Broadcast();
    PROFILE_CPU_END();

    // Sweep DDOL bucket for destroyed objects
	std::erase_if(m_dontDestroyOnLoadObjects, [](Object* o){ return !o || o->IsDestroyMark(); });
}

void SceneManager::Pausing()
{
    if (!m_activeScene) return;
    m_activeScene.load()->UpdateRenderData();
}

void SceneManager::DisableOrEnable()
{
    if (!m_activeScene) return;
    m_activeScene.load()->EndFramePass();
}

void SceneManager::Decommissioning()
{
    // 씬 수를 남긴다. 여기가 예상보다 많으면 목록에 중복이 들어간
    // 것이고, 그것이 종료 시 더블 delete로 번진다(실제로 겪었다).
    std::printf("[SHUTDOWN] Decommissioning 진입(씬 %zu)\n", m_scenes.size());

    if (auto* renderScene = m_ActiveRenderScene.load())
    {
        renderScene->Finalize();
    }

	// DDOL 대상을 먼저 파괴 표시한다. Scene이 소유한 unique_ptr를 해제하기 전에
	// 표시를 세워야 하며, 평상시 DDOL 목록은 비소유 포인터일 뿐이다.
	for (Object* object : m_dontDestroyOnLoadObjects)
		if (object) object->Destroy();
	m_dontDestroyOnLoadObjects.clear();

    for (auto& scene : m_scenes)
    {
        if (scene)
        {
            scene->AllDestroyMark();
            scene->EndFramePass();
        }
    }

	m_detachedDontDestroyOnLoadObjects.clear();

    Memory::SafeDelete(m_inputActionManager);

    WorkerPools->Shutdown();

	PlayModeEvent.Clear();
	InputEvent.Clear();
	SceneRenderingEvent.Clear();
	OnDrawGizmosEvent.Clear();
	GUIRenderingEvent.Clear();
    InternalAnimationUpdateEvent.Clear();
    endOfFrameEvent.Clear();
    sceneLoadedEvent.Clear();
    sceneUnloadedEvent.Clear();
    activeSceneChangedEvent.Clear();
    newSceneCreatedEvent.Clear();
    resourceTrimEvent.Clear();
    m_activeScene = nullptr;
    m_activeSceneIndex = 0;

    for(auto& scene : m_scenes)
    {
        if (scene)
        {
            // EntityHandle은 씬 스코프다 — 이 씬이 죽으면 그 씬 소속으로 등록된
            // 프리팹 인스턴스 항목도 함께 지운다(안 그러면 댕글링 Scene* — P2).
            PrefabUtilitys->ForgetScene(scene);
            delete scene;
        }
	}
}

void SceneManager::SetDecommissioning()
{
    m_exitCommand = true;
}

Scene* SceneManager::CreateScene(std::string_view name)
{
    resourceTrimEvent.Broadcast();
    Scene* allocScene = Scene::CreateNewScene(name);
	Scene* swapScene = nullptr;

	if (!allocScene) return nullptr;

    if (m_activeScene)
    {
		swapScene = m_activeScene.load();
        
        sceneUnloadedEvent.Broadcast();

        swapScene->AllDestroyMark();
        swapScene->EndFramePass();

        // 관리 측 그물은 파괴가 끝난 뒤에 던진다 — 근거는 ClrHost.h의 선언 주석 참고.
        // sceneUnloadedEvent(위)는 파괴 '전'이라 이 자리에 쓸 수 없다.
        ClrHost::Get().NotifySceneUnload();

        std::erase_if(m_scenes,
            [&](const auto& scene) { return scene == swapScene; });

        // EntityHandle은 씬 스코프다 — 이 씬이 죽으면 그 씬 소속으로 등록된
        // 프리팹 인스턴스 항목도 함께 지운다(안 그러면 댕글링 Scene* — P2).
        PrefabUtilitys->ForgetScene(swapScene);

        delete swapScene;
		swapScene = nullptr;
        m_activeScene = allocScene;
    }
    else
    {
        m_activeScene = allocScene;
    }

    m_scenes.push_back(allocScene);
    m_activeSceneIndex = m_scenes.size() - 1;
    allocScene->m_buildIndex = m_activeSceneIndex.load();
    activeSceneChangedEvent.Broadcast();
    newSceneCreatedEvent.Broadcast();

    return allocScene;
}

Scene* SceneManager::SaveScene(std::string_view name)
{
	std::string fileStem = name.data();
	//std::string fileExtension = ".creator";
    file::path saveSceneFileName = fileStem /*+ fileExtension*/;

	// D3-b: 저작 텍스트는 LF로 쓴다. Windows의 텍스트 모드는 개행을 CRLF로 바꾸는데,
	// 그러면 같은 내용을 저장할 때마다 개행이 뒤집혀 git 작업 트리가 흔들린다.
	std::ofstream sceneFileOut(saveSceneFileName, std::ios::binary | std::ios::trunc);
    MetaYml::Node sceneNode{};
	MetaYml::Node assetsBundleNode{};

    m_activeScene.load()->m_Entities[0]->m_name = saveSceneFileName.stem().string();
    try
    {
        sceneNode = Meta::Serialize(m_activeScene.load());
    }
	catch (const std::exception& e)
	{
		Debug->LogError(e.what());
		return nullptr;
	}

    if (0 < m_dontDestroyOnLoadObjects.size())
    {
        MetaYml::Node dontDestroyOnLoadNode;
        for (auto obj : m_dontDestroyOnLoadObjects)
        {
			if (!obj) continue;

			auto* gameObject = dynamic_cast<Entity*>(obj);
            if (gameObject)
            {
				dontDestroyOnLoadNode.push_back(Meta::Serialize(gameObject));
            }
        }
		sceneNode["DontDestroyOnLoadObjects"] = dontDestroyOnLoadNode;
    }

	sceneFileOut << sceneNode;

    sceneFileOut.close();

    return m_activeScene;
}

Scene* SceneManager::LoadSceneImmediate(std::string_view name)
{
	// D0(SerializationPlan): 이 함수 전체가 "씬 전환 1회"를 재는 자다. 하위 단계
	// 합과 이 값의 차이가 곧 미귀속분이고, 그 차이를 숨기지 않는 것이 이 계측의 요점이다.
	SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::SceneLoadTotal);
	std::string loadSceneName = name.data();

	try
	{
        MetaYml::Node sceneNode;
        {
            // D0: 텍스트 → Node 트리 구축 구간만 따로 뗀다.
            SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::SceneParse);
            sceneNode = MetaYml::LoadFile(loadSceneName);
        }
        Scene* swapScene{};
        if (m_activeScene)
        {
            for(auto& object : m_dontDestroyOnLoadObjects)
            {
				auto* go = dynamic_cast<Entity*>(object);
				if (go)
                {
					m_activeScene.load()->DetachEntityHierarchy(
						go, m_detachedDontDestroyOnLoadObjects);
                }
            }

			swapScene = m_activeScene.load();
            sceneUnloadedEvent.Broadcast();
            m_activeScene.load()->AllDestroyMark();
            m_activeScene.load()->EndFramePass();

            // 파괴 뒤에 던진다(ClrHost.h 선언 주석 참고).
            ClrHost::Get().NotifySceneUnload();

            m_activeScene = nullptr;
            
            std::erase_if(m_scenes,
                [&](const auto& scene) { return scene == swapScene; });

            // EntityHandle은 씬 스코프다 — 이 씬이 죽으면 그 씬 소속으로 등록된
            // 프리팹 인스턴스 항목도 함께 지운다(안 그러면 댕글링 Scene* — P2).
            PrefabUtilitys->ForgetScene(swapScene);

            delete swapScene;
        }
		file::path sceneName = name.data();
        resourceTrimEvent.Broadcast();
		m_activeScene = Scene::LoadScene(sceneName.stem().string());

        if(auto assetsBundleNode = sceneNode["m_requiredLoadAssetsBundle"])
        {
            try
            {
                if (assetsBundleNode.IsNull())
                {
                    Debug->LogError("AssetsBundle node is null.");
                }
                else
                {
                    auto* assetBundle = &m_activeScene.load()->m_requiredLoadAssetsBundle;
                    Meta::Deserialize(assetBundle, assetsBundleNode);
                    //DataSystems->LoadAssetBundle(*assetBundle);
                    if (auto assets = assetsBundleNode["assets"])
                    {
                        for (auto asset : assets)
                        {
                            if(asset["assetTypeID"] && asset["assetName"])
                            {
                                AssetEntry entry{};
                                entry.assetTypeID = asset["assetTypeID"].as<int>();
                                entry.assetName = asset["assetName"].as<std::string>();
                                if (!assetBundle->ContainsAsset(entry))
                                {
                                    assetBundle->AddAsset(entry);
                                }
                            }
                        }
                        DataSystems->LoadAssetBundle(*assetBundle);
                    }
                }
            }
            catch (...)
            {
            }
        }

        DataSystems->ClearRetainedAssets();
        DataSystems->RetainAssets(m_dontDestroyOnLoadAssetsBundle);
        DataSystems->RetainAssets(m_activeScene.load()->m_requiredLoadAssetsBundle);

        // m_Entities와 DontDestroyOnLoadObjects 루프 둘 다 같은 씬(m_activeScene)의
        // 슬롯을 할당하므로 배치 하나를 공유한다 — 파일 인덱스가 두 절 사이를
        // 넘나들며 서로를 참조해도(예: DDOL이 일반 오브젝트를 부모로) 안전하다.
        LoadIndexBatch loadBatch;

        for (const auto objNode : SerializedEntities(sceneNode))
        {
            try
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }

                DesirealizeGameObject(type, Authoring::NodeViewAccess::Make(objNode), &loadBatch);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(std::string("Failed to deserialize Entity: ") + e.what());
                continue;
			}
        }

        for (const auto objNode : sceneNode["DontDestroyOnLoadObjects"])
        {
            try
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, Authoring::NodeViewAccess::Make(objNode), &loadBatch);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(std::string("Failed to deserialize DontDestroyOnLoadObject: ") + e.what());
                continue;
			}
		}

        RemapLoadBatchIndices(m_activeScene.load(), loadBatch);

        // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2) — 리매핑 직후, m_Entities·
        // DontDestroyOnLoadObjects 두 절이 공유하는 이 배치 전체를 한 번에 훑는다.
        for (const auto& entry : loadBatch)
        {
            ReconnectPrefabInstance(m_activeScene.load(), entry.object);
        }

        RebindEventDontDestroyOnLoadObjects(m_activeScene.load());
        m_activeScene.load()->AllUpdateWorldMatrix();

		m_scenes.push_back(m_activeScene);
		m_activeSceneIndex = m_scenes.size() - 1;
		activeSceneChangedEvent.Broadcast();
		sceneLoadedEvent.Broadcast();
		// 여기 있던 "플레이어면 재생을 켠다" 분기는 PlayerMain으로 옮겼다(E3-6).
		// 씬 로드가 곧 재생 시작이라는 것은 Player의 정책이지 씬 로더가 알아야 할
		// 일이 아니다 — 로더가 실행 모드를 물어보는 대신 Player가 요청한다.
		// "Scene loaded" 스모크 마커도 함께 갔다(Tools/build.ps1이 소비한다).
        m_activeScene.load()->Reset();
	}
	catch (const std::exception& e)
	{
		Debug->LogError(e.what());
		return nullptr;
	}
	return m_activeScene;
}

Scene* SceneManager::LoadScene(std::string_view name)
{
    std::string loadSceneName = name.data();
    Scene* scene{ nullptr };

    try
    {
        MetaYml::Node sceneNode;
        {
            // D0: 텍스트 → Node 트리 구축 구간만 따로 뗀다.
            SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::SceneParse);
            sceneNode = MetaYml::LoadFile(loadSceneName);
        }
        file::path sceneName = name.data();
        scene = Scene::LoadScene(sceneName.stem().string());

        if (auto assetsBundleNode = sceneNode["AssetsBundle"])
        {
            if (assetsBundleNode.IsNull())
            {
                Debug->LogError("AssetsBundle node is null.");
            }
            else
            {
                Meta::Deserialize(&scene->m_requiredLoadAssetsBundle, assetsBundleNode);
                DataSystems->LoadAssetBundle(scene->m_requiredLoadAssetsBundle);
            }
        }

        // ★ 두 루프가 서로 다른 씬을 타깃으로 한다 — m_Entities는 방금 만든
        // `scene`으로, DontDestroyOnLoadObjects는 (아직 활성화 전인) m_activeScene으로
        // 들어간다(이 함수의 기존 동작을 그대로 유지 — scene.load는 활성 씬을 바꾸지
        // 않는다). 슬롯 인덱스 공간이 서로 다르므로 배치도 둘로 나눈다.
        LoadIndexBatch sceneBatch;
        LoadIndexBatch ddolBatch;

        for (const auto objNode : SerializedEntities(sceneNode))
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }

            DesirealizeGameObject(scene, type, Authoring::NodeViewAccess::Make(objNode), &sceneBatch);
        }

        for (const auto objNode : sceneNode["DontDestroyOnLoadObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }
            DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, Authoring::NodeViewAccess::Make(objNode), &ddolBatch);
        }

        RemapLoadBatchIndices(scene, sceneBatch);
        RemapLoadBatchIndices(m_activeScene.load(), ddolBatch);

        // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2) — 두 배치가 서로 다른
        // 씬을 타깃으로 하므로(위 주석 참고) 리매핑과 마찬가지로 따로 훑는다.
        for (const auto& entry : sceneBatch)
        {
            ReconnectPrefabInstance(scene, entry.object);
        }
        for (const auto& entry : ddolBatch)
        {
            ReconnectPrefabInstance(m_activeScene.load(), entry.object);
        }

        scene->AllUpdateWorldMatrix();


        m_scenes.push_back(scene);
        sceneLoadedEvent.Broadcast();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return nullptr;
    }

	return scene;
}

void SceneManager::SaveSceneAsync(std::string_view name)
{
}

std::future<Scene*> SceneManager::LoadSceneAsync(std::string_view name)
{
    return std::async(std::launch::async, [this, scenePath = std::string(name)]() -> Scene* {
        try
        {
            // This code runs in a background thread.
            MetaYml::Node sceneNode = MetaYml::LoadFile(scenePath);
            Scene* newScene = Scene::LoadScene(std::filesystem::path(scenePath).stem().string());

            if (auto assetsBundleNode = sceneNode["m_requiredLoadAssetsBundle"])
            {
                try
                {
                    if (!assetsBundleNode.IsNull())
                    {
                        auto* assetBundle = &newScene->m_requiredLoadAssetsBundle;
                        if (auto assets = assetsBundleNode["assets"])
                        {
                            for (auto asset : assets)
                            {
                                if (asset["assetTypeID"] && asset["assetName"])
                                {
                                    AssetEntry entry{};
                                    entry.assetTypeID = asset["assetTypeID"].as<int>();
                                    entry.assetName = asset["assetName"].as<std::string>();
                                    if (!assetBundle->ContainsAsset(entry))
                                    {
                                        assetBundle->AddAsset(entry);
                                    }
                                }
                            }
                            DataSystems->LoadAssetBundle(*assetBundle);
                        }
                    }
                }
                catch (...)
                {
                }
            }

            // 두 루프 모두 newScene을 타깃으로 하므로 배치를 공유한다.
            LoadIndexBatch loadBatch;

            for (const auto objNode : SerializedEntities(sceneNode))
            {
                try
                {
                    const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                    if (!type)
                    {
                        Debug->LogError("Failed to extract type from YAML node.");
                        continue;
                    }

                    DesirealizeGameObject(newScene, type, Authoring::NodeViewAccess::Make(objNode), &loadBatch);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(std::string("Failed to deserialize Entity: ") + e.what());
                    continue;
                }
            }

            for (const auto objNode : sceneNode["DontDestroyOnLoadObjects"])
            {
                try
                {
                    const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                    if (!type)
                    {
                        Debug->LogError("Failed to extract type from YAML node.");
                        continue;
                    }
                    DesirealizeDontDestroyOnLoadObjects(newScene, type, Authoring::NodeViewAccess::Make(objNode), &loadBatch);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(std::string("Failed to deserialize DontDestroyOnLoadObject: ") + e.what());
                    continue;
                }
            }

            RemapLoadBatchIndices(newScene, loadBatch);

            // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2).
            for (const auto& entry : loadBatch)
            {
                ReconnectPrefabInstance(newScene, entry.object);
            }

            RebindEventDontDestroyOnLoadObjects(newScene);
            //newScene->AllUpdateWorldMatrix();
            return newScene;
        }
        catch (const std::exception& e)
        {
            Debug->LogError(e.what());
            // Returning nullptr indicates failure. The exception is also stored in the future.
            return nullptr;
        }
    });
}

void SceneManager::LoadSceneAsyncAndWaitCallback(std::string_view name)
{
    // std::launch::async ensures the task runs on a new thread immediately.
    m_loadingSceneFuture = std::async(std::launch::async, [this, scenePath = std::string(name)]() -> Scene* {
        try
        {
            // This code runs in a background thread.
            MetaYml::Node sceneNode = MetaYml::LoadFile(scenePath);
            Scene* newScene = Scene::LoadScene(std::filesystem::path(scenePath).stem().string());

            if (auto assetsBundleNode = sceneNode["m_requiredLoadAssetsBundle"])
            {
                if (!assetsBundleNode.IsNull())
                {
                    auto* assetBundle = &newScene->m_requiredLoadAssetsBundle;
                    if (auto assets = assetsBundleNode["assets"])
                    {
                        for (auto asset : assets)
                        {
                            if (asset["assetTypeID"] && asset["assetName"])
                            {
                                AssetEntry entry{};
                                entry.assetTypeID = asset["assetTypeID"].as<int>();
                                entry.assetName = asset["assetName"].as<std::string>();
                                if (!assetBundle->ContainsAsset(entry))
                                {
                                    assetBundle->AddAsset(entry);
                                }
                            }
                        }
                        DataSystems->LoadAssetBundle(*assetBundle);
                    }
                }
            }

            // ★ LoadScene(비-즉시 버전)과 같은 이유로 두 루프가 서로 다른 씬을
            // 타깃으로 한다 — m_Entities는 newScene, DontDestroyOnLoadObjects는
            // (아직 활성화 전인) m_activeScene. 기존 동작 그대로 유지하고 배치만 나눈다.
            LoadIndexBatch sceneBatch;
            LoadIndexBatch ddolBatch;

            for (const auto objNode : SerializedEntities(sceneNode))
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type) {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeGameObject(newScene, type, Authoring::NodeViewAccess::Make(objNode), &sceneBatch);
            }

            for (const auto objNode : sceneNode["DontDestroyOnLoadObjects"])
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, Authoring::NodeViewAccess::Make(objNode), &ddolBatch);
            }

            RemapLoadBatchIndices(newScene, sceneBatch);
            RemapLoadBatchIndices(m_activeScene.load(), ddolBatch);

            // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2) — 두 배치가 서로 다른
            // 씬을 타깃으로 하므로(위 주석 참고) 리매핑과 마찬가지로 따로 훑는다.
            for (const auto& entry : sceneBatch)
            {
                ReconnectPrefabInstance(newScene, entry.object);
            }
            for (const auto& entry : ddolBatch)
            {
                ReconnectPrefabInstance(m_activeScene.load(), entry.object);
            }

            RebindEventDontDestroyOnLoadObjects(newScene);

            newScene->AllUpdateWorldMatrix();
            return newScene;
        }
        catch (const std::exception& e)
        {
            Debug->LogError(e.what());
            // Returning nullptr indicates failure. The exception is also stored in the future.
            return nullptr;
        }
    });
}

void SceneManager::ActivateScene(Scene* sceneToActivate, bool isOldSceneDelete)
{
    if (!sceneToActivate) return;

	m_sceneToActivate = sceneToActivate;
	m_isOldSceneDelete = isOldSceneDelete;
}

void SceneManager::BeforeAwakeSceneLoad()
{
    if (m_sceneToActivate.load())
    {
        Benchmark debugTimer;
        Scene* oldScene{};
        if (m_activeScene.load())
        {
            oldScene = m_activeScene.load();
            oldScene->ResetSelectedEntity();

            for (auto& object : m_dontDestroyOnLoadObjects)
            {
				auto* go = dynamic_cast<Entity*>(object);
                if (go)
                {
					m_activeScene.load()->DetachEntityHierarchy(
						go, m_detachedDontDestroyOnLoadObjects);
                }
            }

            UIManagers->ClearSelectUI();
            sceneUnloadedEvent.Broadcast();
            m_activeScene.load()->AllDestroyMark();
            m_activeScene.load()->EndFramePass();

            // 파괴 뒤에 던진다(ClrHost.h 선언 주석 참고).
            ClrHost::Get().NotifySceneUnload();

            //m_activeScene = nullptr;

            if (m_isOldSceneDelete)
            {
                std::erase_if(m_scenes, [&](const auto& scene) { return scene == oldScene; });
            }

        }

        //resourceTrimEvent.Broadcast();
        m_activeScene = m_sceneToActivate.load();

        // ★ 이미 목록에 있으면 다시 넣지 않는다.
        //
        // scene.switch는 LoadScene으로 씬을 연 뒤 ActivateScene을 부른다.
        // LoadScene이 이미 m_scenes에 넣었는데 여기서 또 넣으면 같은
        // 포인터가 두 번 들어간다. 위의 erase_if는 옛 씬만 지우므로
        // 그 중복을 막지 못한다.
        //
        // 그 결과가 종료 시 더블 delete다. 두 번째 delete가 이미 파괴된
        // Scene의 델리게이트를 만지고, 그 스핀락 플래그가 쓰레기 값으로
        // set이면 Clear()가 영원히 돈다 — 크래시가 아니라 무한 대기라
        // '종료가 가끔 멈춘다'로만 보였다.
        const auto found = std::find(m_scenes.begin(), m_scenes.end(),
            m_sceneToActivate.load());
        if (found == m_scenes.end())
        {
            m_scenes.push_back(m_sceneToActivate);
            m_activeSceneIndex = m_scenes.size() - 1;
        }
        else
        {
            m_activeSceneIndex = static_cast<size_t>(
                std::distance(m_scenes.begin(), found));
        }
        // Debug log the time taken to activate the scene
        Debug->Log(std::string("Scene activation took ") + std::to_string(debugTimer.GetElapsedTime()) + " ms.");

        Benchmark debugTimer1;

        RebindEventDontDestroyOnLoadObjects(m_sceneToActivate.load());
        m_activeScene.load()->AllUpdateWorldMatrix();

        activeSceneChangedEvent.Broadcast();
        sceneLoadedEvent.Broadcast();

        m_activeScene.load()->Reset();

        if (m_isOldSceneDelete)
        {
            // EntityHandle은 씬 스코프다 — 이 씬이 죽으면 그 씬 소속으로 등록된
            // 프리팹 인스턴스 항목도 함께 지운다(안 그러면 댕글링 Scene* — P2).
            PrefabUtilitys->ForgetScene(oldScene);
            delete oldScene;
        }
        m_sceneToActivate = nullptr;
        Debug->Log(std::string("Rebinding DDOL and updating world matrices took ") + std::to_string(debugTimer1.GetElapsedTime()) + " ms.");

        // 새 씬의 보존 목록(RetainAssets)이 갱신된 뒤이므로 이 시점에 캐시를 정리한다.
        //
        // 이 호출은 오랫동안 존재만 하고 불리지 않았다. 소유권 구조상 켜면 사용 중인
        // 에셋까지 파괴됐기 때문이다(12.2 보충 분석). 이제 컴포넌트·프록시·Model이
        // shared_ptr로 공동 소유하므로(2-2~2-5) 캐시에서 지워도 참조가 남아 있으면
        // 살아 있고, 아무도 쓰지 않는 것만 실제로 해제된다.
        DataSystems->UnloadUnusedAssets();

        // 관리 힙도 같은 경계에서 정리한다(PHASE 9-6).
        //
        // 네이티브 캐시만 비우면 반쪽이다 — 파괴된 씬의 Behaviour와 그 필드가 잡고
        // 있던 관리 객체는 GC가 돌아야 사라지고, 그 시점을 런타임에 맡기면 다음 씬
        // 한복판에서 일어난다. 그러면 프레임이 튀고, 그 튐이 재설계 탓인지 GC 탓인지
        // 구분되지 않는다. 두 힙을 같은 지점에서 평탄하게 만들어야 씬 churn 벤치의
        // "제자리로 돌아왔는가"가 성립한다(2-9 판정 기준 확장).
        //
        // 이 자리인 이유: 새 씬의 RetainAssets 갱신과 UnloadUnusedAssets가 끝난 뒤라
        // 이제 아무도 참조하지 않는 것이 확정된 상태다. 앞에서 부르면 곧 버려질
        // 참조가 아직 살아 있어 회수되지 않는다.
        ClrHost::Get().CollectManagedHeap();

        // 씬 전환마다 GPU 객체/VRAM 증감을 남긴다.
        // 같은 씬을 오가며 이 값이 계속 증가하면 회수되지 않는 리소스가 있다는 뜻이다.
        // 실행 중에는 VRAM 증감만 남는다(타입별 집계는 디버그 레이어를 망가뜨린다).
        GpuDiagnostics::LogDelta("씬 전환 완료");
    }
}

bool SceneManager::IsSceneLoading() const
{
    return m_sceneToActivate != nullptr;
}

void SceneManager::AddDontDestroyOnLoad(Object* objPtr)
{
	if (!objPtr) return;
	if (std::ranges::find(m_dontDestroyOnLoadObjects, objPtr) == m_dontDestroyOnLoadObjects.end())
		m_dontDestroyOnLoadObjects.push_back(objPtr);
}

void SceneManager::RemoveDontDestroyOnLoad(Object* objPtr)
{
    if (objPtr)
    {
        std::erase_if(m_dontDestroyOnLoadObjects,
			[&](const auto& obj) { return obj == objPtr; });
	}
}

void SceneManager::RebindEventDontDestroyOnLoadObjects(Scene* scene)
{
    if (!scene) return;
    if (m_dontDestroyOnLoadObjects.empty()) return;
	// 비동기 LoadScene은 새 Scene이 준비되기 전에 이 함수에 들어올 수 있다. 아직
	// Detach가 만든 unique_ptr transfer가 없다면 옛 Scene의 엔티티를 목적 Scene에
	// 중복 등록하지 않는다. 실제 이송이 준비된 경우에만 아래를 실행한다.
	if (m_detachedDontDestroyOnLoadObjects.empty()) return;

    // DDOL 루트들을 모아 한 번에 부착(서브트리 포함).
    // 씬 공식 API로 부착(유니크 네임/Tag/Layer/루트 children/Transform 부모 세팅 포함)
	auto remap = scene->AttachExistingEntityHierarchy(m_detachedDontDestroyOnLoadObjects);
    (void)remap;

    // 컴포넌트 생명주기 재등록.
    //
    // DDOL 오브젝트는 씬을 건너 살아남으므로 새 씬의 디스패치 대상에 다시 넣어야 한다.
    // 이 경로를 빠뜨리면 씬 전환 후 DDOL 오브젝트만 조용히 틱을 못 받는다 —
    // 증상이 '가끔 안 움직인다'라 원인을 짚기 어려운 종류다.
	for (Object* obj : m_dontDestroyOnLoadObjects)
    {
		auto* go = dynamic_cast<Entity*>(obj);
        if (!go) continue;

        for (auto& comp : go->m_components)
        {
            if (!comp) continue;

            scene->RegisterComponent(comp.get());
        }
    }
}

std::vector<MeshRenderer*> SceneManager::GetAllMeshRenderers() const
{
	return m_activeScene.load()->m_allMeshRenderers;
}

std::vector<std::shared_ptr<Material>>
SceneManager::CaptureRequiredRenderMaterials() const
{
    std::vector<std::shared_ptr<Material>> materials;
    std::unordered_set<const Material*> seen;
    Scene* scene = m_activeScene.load();
    if (nullptr == scene) return materials;

    const auto add = [&materials, &seen](const std::shared_ptr<Material>& material)
    {
        if (!material || !seen.insert(material.get()).second) return;
        materials.push_back(material);
    };

    for (const MeshRenderer* renderer : scene->m_allMeshRenderers)
    {
        if (nullptr != renderer) add(renderer->m_Material);
    }
    for (const FoliageComponent* foliage : scene->m_foliageComponents)
    {
        if (nullptr == foliage) continue;
        for (const FoliageType& type : foliage->GetFoliageTypes())
            add(type.m_material);
    }
    return materials;
}

void SceneManager::VolumeProfileApply()
{
	m_volumeProfileApply = true;
}

// ── 씬 스냅샷 primitive (E3-1) ──
//
// 선언부 주석 참고. 여기서는 "무엇을 하는가"만 담고 "누가 언제 부르는가"는
// transaction 쪽이 정한다.

bool SceneManager::HasSceneSnapshot() const
{
    // D3-a-3: 문서 자체의 빈 상태는 타입이 답하고, 그 안의 엔티티 목록만 노드로 본다.
    if (m_editorSceneBackup.IsEmpty()) return false;
    return static_cast<bool>(
        SerializedEntities(Authoring::DocumentAccess::Node(m_editorSceneBackup)));
}

void SceneManager::DiscardSceneSnapshot()
{
    m_editorSceneBackup.Clear();
}

bool SceneManager::CaptureSceneSnapshot()
{
    Scene* scene = m_activeScene.load();
    if (nullptr == scene)
    {
        return false;
    }

    try
    {
        PROFILE_CPU_BEGIN("Serialize");
        // 편집 중이던 최신 값이 담기도록 직렬화한다. 스크립트를 재우지 않는
        // 이유는 사본이 없어 두 벌이 생기지 않기 때문이다 — 지금 씬의 인스턴스가
        // 그대로 플레이 인스턴스가 된다.
        m_editorSceneBackup = Authoring::DocumentAccess::Adopt(Meta::Serialize(scene));
        PROFILE_CPU_END();
    }
    catch (const std::exception& e)
    {
        // ⚠ 의도적 차이. 옛 코드의 catch는 m_editorSceneBackup을 건드리지 않고
        // 그냥 return 했다. 그러면 **직전 재생 세션의 백업이 그대로 남는다** —
        // 그 뒤 정지를 누르면 "백업이 있다" 검사를 통과해, 지금 씬과 무관한
        // 과거 씬의 오브젝트를 현재 씬에 역직렬화해 섞어 넣는다.
        //
        // 도달 경로가 실재한다: LoadSceneImmediate가 m_activeScene을 널로 만든 뒤
        // Scene::LoadScene이 던지면 바깥 catch가 로그만 남기고 널을 남긴다 →
        // 정지하면 EndPlayTransaction이 널-씬 조기 반환으로 백업을 남긴 채 빠진다
        // → 씬을 다시 열고 재생하면 이 catch에 닿는다.
        //
        // 스냅샷을 비우는 편이 옳다. 다만 이 거부는 조용하면 안 되므로 로그가 남는다.
        Debug->LogError(e.what());
        DiscardSceneSnapshot();
        return false;
    }

    if (!HasSceneSnapshot())
    {
        // 직렬화 자체는 던지지 않았는데 엔티티 노드가 없는 경우다. 옛 코드는 이때도
        // 재생에 들어갔고, 정지할 때 비로소 "백업이 없어 복원하지 못했다"가 떠서
        // 편집 내용을 잃었다. 지금은 들어가지 않는다 — 다만 그 거부가 조용하면
        // "재생 버튼이 안 먹는다"로만 보이므로 반드시 남긴다.
        Debug->LogError("[PIE] 씬 스냅샷이 비어 있어 재생에 들어가지 않는다");
        DiscardSceneSnapshot();
        return false;
    }

    return true;
}

bool SceneManager::RestoreSceneSnapshot()
{
    Scene* scene = m_activeScene.load();
    if (nullptr == scene || !HasSceneSnapshot())
    {
        return false;
    }

    // 살아남은 오브젝트(DontDestroyOnLoad)는 백업에도 실려 있다. 그대로
    // 역직렬화하면 같은 객체가 한 벌 더 생기므로 instanceID로 걸러낸다.
    // DDOL을 파괴하지 않는 것은 현재 엔진의 정책을 그대로 둔 것이다 —
    // 유니티는 재생 종료 때 DDOL도 버리지만, 그 정책 변경은 이 수정의
    // 범위 밖이고 회귀 범위가 훨씬 넓다.
    std::unordered_set<size_t> survivingIds;
    for (const auto& object : scene->m_Entities)
    {
        if (object) survivingIds.insert(object->GetInstanceID());
    }

    try
    {
        PROFILE_CPU_BEGIN("RestoreEditorScene");
        LoadIndexBatch loadBatch;
        for (const auto& objNode :
            SerializedEntities(Authoring::DocumentAccess::Node(m_editorSceneBackup)))
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }

            if (objNode["m_instanceID"] &&
                survivingIds.contains(objNode["m_instanceID"].as<size_t>()))
            {
                continue;
            }

            DesirealizeGameObject(type, Authoring::NodeViewAccess::Make(objNode), &loadBatch);
        }
        RemapLoadBatchIndices(scene, loadBatch);

        // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2) — DDOL로 살아남아
        // 되먹인 오브젝트(survivingIds로 걸러짐)는 이미 등록돼 있으니 중복 등록은
        // RegisterInstance의 existing-check가 걸러준다.
        for (const auto& entry : loadBatch)
        {
            ReconnectPrefabInstance(scene, entry.object);
        }

        PROFILE_CPU_END();

        // InScene으로 되돌린다(트랙 L1) — 방금 복원한 오브젝트는 GameObject
        // 생성자의 기본값이 이미 InScene이지만, DDOL이라 파괴 없이 살아남은
        // 오브젝트는 Simulating에 머문 채라 여기서 함께 맞춘다.
        SetSimulationPhase(ScenePhase::InScene);

        scene->AllUpdateWorldMatrix();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return false;
    }

    return true;
}

// ── 시뮬레이션 primitive (E3-1) ──
//
// OnBeginSimulation 같은 훅을 여기서 부르지 않는다 — Start()는 이미 매 프레임 드는
// pendingAwake/Start 드레인이 State_StartCalled 가드로 정확히 한 번만 부르고 있어
// (에디터 틱도 예외가 아니다), 여기서 다시 부르면 그 가드를 건너뛰고 두 번 불린다.
// phase 필드는 그 드레인과 무관하게 상태 기계 자체를 정확히 유지하기 위한 부기다.
void SceneManager::SetSimulationPhase(ScenePhase phase)
{
    Scene* scene = m_activeScene.load();
    if (nullptr == scene) return;

    for (auto& obj : scene->m_Entities)
    {
        if (obj) obj->m_scenePhase = phase;
    }
}

void SceneManager::BeginPlayTransaction()
{
    // 재생 시작. 씬을 복제하지 않고 지금 씬을 그대로 플레이한다.
    //
    // 예전에는 에디터 씬을 직렬화해 PlayScene을 따로 만들고 활성 씬을 그쪽으로
    // 옮겼다. 그러면 두 씬이 메모리에 공존하는데, 렌더는 RenderScene 하나에
    // 모든 프록시를 모아 두고 씬 구분 없이 그리므로 에디터 씬 오브젝트가
    // 플레이 화면에 함께 보였다. 로직 쪽에서도 같은 문제가 먼저 나와
    // SuspendSceneScripts로 스크립트가 두 벌 도는 것을 막고 있었다.
    //
    // 유니티가 쓰는 방식으로 바꾼다 — 씬을 백업해 두고 그 씬 자체로 플레이한 뒤
    // 정지할 때 백업으로 되돌린다. 씬이 하나뿐이므로 '두 벌' 문제가 계층마다
    // 반복될 여지가 사라진다. (언리얼은 반대로 PIE 월드를 복제하되 월드마다
    // FScene을 따로 두는 쪽이다. 어느 한쪽을 온전히 따라야 하고, 지금 구조는
    // 렌더 씬이 하나이므로 이쪽이 맞다.)
    try
    {
        // Editor 정책을 걸 자리. 예전에는 여기서 Undo 스택을 직접 비웠는데, 이
        // 함수는 Player도 타므로(Player의 유일한 재생 진입 경로다) 출하 게임이
        // 매번 Undo를 비우고 있었다. 지금은 통지만 하고, EditorPlayModeController가
        // 구독해 그 일을 한다. Player는 구독자가 없어 아무 일도 일어나지 않는다.
        PROFILE_CPU_BEGIN("PlayModeEvent(enter)");
        PlayModeEvent.Broadcast(true);
        PROFILE_CPU_END();

        // 직렬화가 실패하면 phase를 올리지 않는다 — 되돌릴 기준이 없는 채로
        // 재생에 들어가면 정지할 때 씬을 잃는다.
        if (!CaptureSceneSnapshot())
        {
            return;
        }
        resourceTrimEvent.Broadcast();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return;
    }

    // Simulating 전이(SceneGraphRedesignPlan §4 트랙 L1) — 씬을 복제하지 않으므로
    // 이 시점의 오브젝트 전원이 곧 플레이 인스턴스다.
    SetSimulationPhase(ScenePhase::Simulating);
}

void SceneManager::EndPlayTransaction()
{
    // 재생 정지. 같은 Scene 객체를 비우고 백업으로 되채운다.
    Scene* scene = m_activeScene.load();
    if (nullptr == scene)
    {
        m_isEditorSceneLoaded = false;
        return;
    }

    if (!HasSceneSnapshot())
    {
        // 백업이 없으면 되돌릴 기준이 없다. 씬을 비우면 복구 불가능한 손실이
        // 되므로 그대로 둔다 — 재생 중 상태가 남는 편이 빈 씬보다 낫다.
        Debug->LogError("[PIE] 에디터 씬 백업이 없어 복원하지 못했다");
        m_isEditorSceneLoaded = false;
        return;
    }

    resetSelectedObjectEvent.Broadcast();
    sceneUnloadedEvent.Broadcast();

    scene->AllDestroyMark();
    scene->EndFramePass();

    // 파괴 뒤에 던진다(ClrHost.h 선언 주석 참고). 이 경로는 특히 DDOL이 살아남는
    // 자리라, 파괴 전에 부르는 형태였다면 재생 종료마다 DDOL 스크립트가 죽었을 것이다.
    ClrHost::Get().NotifySceneUnload();

    RestoreSceneSnapshot();

    DiscardSceneSnapshot();

    // 이탈 통지. 씬이 복원된 뒤에 던진다 — 구독자가 씬을 들여다볼 수 있어야 한다.
    // (현재 Editor 구독자는 진입만 쓰지만, 대칭을 지켜 두어야 이탈 정책이 생길 때
    //  자리를 다시 정하지 않는다.)
    PlayModeEvent.Broadcast(false);

    activeSceneChangedEvent.Broadcast();
    sceneLoadedEvent.Broadcast();

	m_isEditorSceneLoaded = false;
}

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const Authoring::NodeView& view, LoadIndexBatch* batch)
{
	const Authoring::ReadNode itNode = Authoring::NodeViewAccess::Node(view);
    if (type->typeID == type_guid(Entity))
    {
        // 프리팹 재연결(P-a)은 더 이상 여기서 오브젝트 하나씩 하지 않는다 — 이 배치가
        // 로드된 뒤 RemapLoadBatchIndices까지 끝나야 m_parentIndex가 슬롯 인덱스로
        // 확정되므로, 재연결은 로더 각 호출부가 배치 전체를 한 번에 훑는다
        // (ReconnectPrefabInstance, SceneGraphRedesignPlan P2).

		Entity::SerializedHierarchy serializedHierarchy =
			EntityAuthoring::ReadSerializedHierarchy(itNode);
		auto obj = m_activeScene.load()->LoadEntity(
            itNode["m_instanceID"].As<size_t>(),
            itNode["m_name"].AsString(),
			EntityAuthoring::InferCreationType(itNode),
			Entity::INVALID_INDEX
		);

        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — Deserialize가
            // YAML의 m_index로 obj->m_index를 덮어쓰는데, 부분 편집·병합된 씬 파일은 이
            // 값이 실제 슬롯 위치와 어긋날 수 있다. LoadEntity가 갓 부여한 값(=실제
            // 슬롯 위치)을 미리 붙잡아 뒀다가, 덮어써진 뒤 어긋나면 정정한다.
            //
            // 같은 배치의 다른 오브젝트가 든 m_parentIndex/m_childrenIndices/m_rootIndex는
            // 여전히 파일 인덱스 스킴이다 — 이 함수만으로는 고칠 수 없다(아직 로드되지
            // 않은 오브젝트를 참조할 수 있으므로). 그래서 여기서는 정정만 하고, 파일
            // 값을 배치 버퍼에 남겨 로드 루프가 끝난 뒤 RemapLoadBatchIndices가 한 번에
            // 고치게 한다.
            const Entity::Index actualSlotIndex = obj->m_index;

            {
                // D0: 엔티티 리플렉션 순회 구간(컴포넌트 특례는 아래 ComponentLoad).
                SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::EntityDeserialize);
                Meta::Deserialize(obj, itNode);
            }

            const Entity::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
				batch->push_back({ obj, fileIndex,
					serializedHierarchy.parentIndex,
					serializedHierarchy.rootIndex,
					std::move(serializedHierarchy.childrenIndices) });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

            // 구파일 승격(레인 2, SceneGraphRedesignPlan §5 예외 4) — 구스키마 m_transform 키가 있으면 Transform 컴포넌트에 값을 쓴다(신파일은 무작용).
            LegacyTransformPromotion::PromoteLegacyTransform(obj, itNode);
        }

        if (itNode["m_components"])
        {
            const Authoring::ReadNode componentsNode = itNode["m_components"];
            for (const auto componentNode : componentsNode)
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, Authoring::NodeViewAccess::Make(componentNode), m_isGameStart);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(e.what());
                    continue;
                }
            }
        }
    }
}

void SceneManager::DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const Authoring::NodeView& view, LoadIndexBatch* batch)
{
	const Authoring::ReadNode itNode = Authoring::NodeViewAccess::Node(view);
    if (type->typeID == type_guid(Entity))
    {
        // 프리팹 재연결(P-a)은 더 이상 여기서 오브젝트 하나씩 하지 않는다 — 이유는
        // 위 오버로드 주석 참고(ReconnectPrefabInstance, SceneGraphRedesignPlan P2).

		Entity::SerializedHierarchy serializedHierarchy =
			EntityAuthoring::ReadSerializedHierarchy(itNode);
		auto obj = targetScene->LoadEntity(
            itNode["m_instanceID"].As<size_t>(),
            itNode["m_name"].AsString(),
			EntityAuthoring::InferCreationType(itNode),
			Entity::INVALID_INDEX
		);

        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — 위와 동일한
            // 이유로 여기서도 검증한다(DDOL이 아닌 일반 씬 오브젝트 로드 경로). 배치
            // 버퍼에 파일 인덱스를 남기는 이유는 위 오버로드 주석 참고.
            const Entity::Index actualSlotIndex = obj->m_index;

            {
                // D0: 엔티티 리플렉션 순회 구간(컴포넌트 특례는 아래 ComponentLoad).
                SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::EntityDeserialize);
                Meta::Deserialize(obj, itNode);
            }

            const Entity::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
				batch->push_back({ obj, fileIndex,
					serializedHierarchy.parentIndex,
					serializedHierarchy.rootIndex,
					std::move(serializedHierarchy.childrenIndices) });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

            // 구파일 승격(레인 2, SceneGraphRedesignPlan §5 예외 4) — 구스키마 m_transform 키가 있으면 Transform 컴포넌트에 값을 쓴다(신파일은 무작용).
            LegacyTransformPromotion::PromoteLegacyTransform(obj, itNode);
        }

        if (itNode["m_components"])
        {
            const Authoring::ReadNode componentsNode = itNode["m_components"];
            for (const auto componentNode : componentsNode)
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, Authoring::NodeViewAccess::Make(componentNode), m_isGameStart);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(e.what());
                    continue;
                }
            }
        }
    }
}

void SceneManager::DesirealizeDontDestroyOnLoadObjects(Scene* targetScene, const Meta::Type* type, const Authoring::NodeView& view, LoadIndexBatch* batch)
{
	const Authoring::ReadNode itNode = Authoring::NodeViewAccess::Node(view);
    if (type->typeID == type_guid(Entity))
    {
        auto it = std::find_if(m_dontDestroyOnLoadObjects.begin(), m_dontDestroyOnLoadObjects.end(),
			[&](const auto& obj) { return obj->GetInstanceID() == itNode["m_instanceID"].As<size_t>(); });
        if(it != m_dontDestroyOnLoadObjects.end())
        {
            Debug->LogWarning("Object with instance ID " + std::to_string(itNode["m_instanceID"].As<size_t>()) + " already exists in DontDestroyOnLoad.");
            return; // Object already exists, skip deserialization
		}

		Entity::SerializedHierarchy serializedHierarchy =
			EntityAuthoring::ReadSerializedHierarchy(itNode);
		auto obj = targetScene->LoadEntity(
            itNode["m_instanceID"].As<size_t>(),
            itNode["m_name"].AsString(),
			EntityAuthoring::InferCreationType(itNode),
			Entity::INVALID_INDEX
		);
        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — DDOL 로드
            // 경로도 예외가 아니다. 배치 버퍼에 파일 인덱스를 남기는 이유는
            // DesirealizeGameObject 오버로드 주석 참고.
            const Entity::Index actualSlotIndex = obj->m_index;

            {
                // D0: 엔티티 리플렉션 순회 구간(컴포넌트 특례는 아래 ComponentLoad).
                SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::EntityDeserialize);
                Meta::Deserialize(obj, itNode);
            }

            const Entity::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
				batch->push_back({ obj, fileIndex,
					serializedHierarchy.parentIndex,
					serializedHierarchy.rootIndex,
					std::move(serializedHierarchy.childrenIndices), true });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }
            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

            // 구파일 승격(레인 2, SceneGraphRedesignPlan §5 예외 4) — 구스키마 m_transform 키가 있으면 Transform 컴포넌트에 값을 쓴다(신파일은 무작용).
            LegacyTransformPromotion::PromoteLegacyTransform(obj, itNode);
        }
        if (itNode["m_components"])
        {
            const Authoring::ReadNode componentsNode = itNode["m_components"];
            for (const auto componentNode : componentsNode)
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, Authoring::NodeViewAccess::Make(componentNode), m_isGameStart);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(e.what());
                    continue;
                }
            }
        }
	}
}

void SceneManager::RemapLoadBatchIndices(Scene* targetScene, LoadIndexBatch& batch)
{
    if (!targetScene || batch.empty()) return;

    // 파일 인덱스 → 실제 슬롯 인덱스. 이 배치에서 막 로드된 오브젝트만 담는다 —
    // 이전에 붙어 있던 DDOL이나 다른 배치의 오브젝트는 여기서 다루지 않는다
    // (호출부가 로더/타깃 씬 단위로 배치를 나눠 넘긴다).
    std::unordered_map<Entity::Index, Entity::Index> fileToSlot;
    fileToSlot.reserve(batch.size() + 1);
    std::unordered_set<Entity::Index> batchSlots;
    batchSlots.reserve(batch.size());

    // 합성 루트(슬롯 0)는 씬이 서 있는 동안 절대 재할당되지 않는다(Scene::ReleaseSlot의
    // "index==0은 해제하지 않는다" 참고) — 파일에 적힌 루트의 m_index도 항상 0이다.
    // 이 배치에 루트 자신의 로드 엔트리가 없어도(예: RestoreSceneSnapshot 복원은
    // 살아남은 루트를 다시 로드하지 않고 건너뛴다) "부모가 루트"를 가리키는 흔한
    // 참조가 데이터 오염으로 오탐되지 않도록 미리 채워 둔다.
    if (!targetScene->m_Entities.empty() && targetScene->m_Entities[0])
    {
        fileToSlot[0] = 0;
    }

    size_t mismatchCount = 0;
    for (const auto& entry : batch)
    {
        if (!entry.object) continue;
        fileToSlot[entry.fileIndex] = entry.object->m_index;
        batchSlots.insert(entry.object->m_index);
        if (entry.fileIndex != entry.object->m_index)
        {
            ++mismatchCount;
        }
    }

    // 어긋남은 이제 예외가 아니라 일상이다(FT_Material 14건 전부 반전 등 실측) —
    // 오브젝트별 스팸 대신 배치당 한 줄 요약만 남긴다.
    if (mismatchCount > 0)
    {
        Debug->LogWarning("[Scene] '" + targetScene->GetSceneName().ToString() + "' 로드: m_index 불일치 "
            + std::to_string(mismatchCount) + "/" + std::to_string(batch.size())
            + "건을 슬롯 위치로 정정하고, 배치 내 계층 참조(m_parentIndex/m_childrenIndices/m_rootIndex)를 "
            + "슬롯 인덱스로 리매핑합니다.");
    }

    // 배치 안에 없는 참조(진짜 데이터 오염) — 건별로 남긴다.
    const auto remapOrLog = [&](Entity::Index fileIdx, Entity* owner, const char* label) -> Entity::Index
    {
        auto foundIt = fileToSlot.find(fileIdx);
        if (foundIt != fileToSlot.end())
        {
            return foundIt->second;
        }

        Debug->LogError("[Scene] '" + targetScene->GetSceneName().ToString() + "' Entity '"
            + (owner ? owner->m_name.ToString() : std::string("?")) + "'의 " + label
            + " 참조(파일 인덱스 " + std::to_string(fileIdx) + ")가 이 배치 안에 없습니다 — 데이터 오염 가능성.");
        return Entity::INVALID_INDEX;
    };

    for (auto& entry : batch)
    {
        Entity* obj = entry.object;
        if (!obj) continue;

        // 파일 parent: 못 찾으면 씬 루트(kSceneRootIndex)로 편입한다.
        //
        // 예전에는 INVALID_INDEX로 남겨 뒀는데, 그러면 "루트 children에는 실렸으면서
        // 부모는 없다"는 어긋난 쌍이 로드 결과에 그대로 남는다. 표기를 하나로 모은
        // 뒤로는(Scene::AttachExistingEntity 주석) 여기서도 루트를 가리킨다.
        // 씬 합성 루트(0) 자신은 부모가 없으므로 그대로 무효로 남긴다.
        if (Entity::IsValidIndex(entry.fileParentIndex))
        {
			Entity::Index remappedParent = remapOrLog(
				entry.fileParentIndex, obj, "부모(m_parentIndex)");
			obj->SetParentIndex(Entity::IsValidIndex(remappedParent)
				? remappedParent : Entity::kSceneRootIndex);
        }
		else if (Entity::kSceneRootIndex != obj->m_index)
		{
			obj->SetParentIndex(Entity::kSceneRootIndex);
		}
		else
		{
			// Transform 컴포넌트 로드가 SetOwner를 다시 타며 bootstrap parent를
			// 덮을 수 있다. 합성 루트까지 명시적으로 복원하지 않으면 parentID=0인
			// self-parent가 남아 월드 행렬 평가가 무한 순회한다.
			obj->SetParentIndex(Entity::INVALID_INDEX);
		}

        // m_childrenIndices: 합성 루트(0)는 아래에서 부모 포인터 기준으로 통째로
        // 재구성하므로 여기서는 건드리지 않는다 — 두 출처가 섞이면(파일이 적어 둔
        // children 목록 vs 자식들의 실제 parentIndex) 어느 쪽이 진실인지 알 수 없다.
        if (obj->m_index != 0)
        {
            std::vector<Entity::Index> remappedChildren;
			remappedChildren.reserve(entry.fileChildrenIndices.size());
			for (Entity::Index childFileIdx : entry.fileChildrenIndices)
            {
                if (!Entity::IsValidIndex(childFileIdx)) continue;

                Entity::Index remapped = remapOrLog(childFileIdx, obj, "자식(m_childrenIndices)");
                if (Entity::IsValidIndex(remapped))
                {
                    remappedChildren.push_back(remapped);
                }
                // 못 찾으면 목록에서 뺀다 — 위에서 이미 개별 LogError를 남겼다.
            }
            obj->SetChildrenIndices(std::move(remappedChildren));
        }
        else
        {
            obj->ClearChildren();
        }

        // m_rootIndex(스켈레톤 본 팔레트 등이 쓰는 루트 뼈 참조) — INVALID_INDEX는
        // "루트 뼈 없음/분리됨"이라는 유효한 상태다(Object::SetDontDestroyOnLoad가
        // 명시적으로 이 값을 쓰고, UpdateModelRecursive의 Bone 분기는 TryGetEntity가
        // nullptr을 돌려주면 조용히 건너뛴다) — 건드리지 않는다. 유효한 파일 인덱스인데
        // 배치 안에 없을 때만 진짜 오염이므로, 그때만 자기 자신으로 대체한다(체인이
        // 끊기는 것보다 안전한 폴백).
        if (Entity::IsValidIndex(entry.fileRootIndex))
        {
			Entity::Index remapped = remapOrLog(
				entry.fileRootIndex, obj, "루트 뼈(m_rootIndex)");
            obj->SetRootIndex(Entity::IsValidIndex(remapped) ? remapped : obj->m_index);
        }
		else
		{
			obj->SetRootIndex(Entity::INVALID_INDEX);
		}
    }

    // 합성 루트(0)의 children을 배치 기준으로 재구성한다. 진실의 원천은 자식들의
    // 최종 m_parentIndex다(위 루프에서 이미 슬롯 인덱스로 확정됐다) — 루트 자신이
    // 파일에서 읽어 온 children 목록은 신뢰하지 않는다. 그래야 "부모는 루트를
    // 가리키는데 루트의 목록엔 없다" 같은 비대칭 오염도 자연히 치유된다.
    //
    // LoadEntity는 CreateEntity/AddEntity와 달리 부모의 children에
    // 자동으로 얹지 않으므로(Scene::LoadEntity 참고), 여기서 지우고 다시
    // 채워도 이 배치가 로드되는 동안 다른 경로가 끼워 넣은 값과 섞이지 않는다.
    if (!targetScene->m_Entities.empty() && targetScene->m_Entities[0])
    {
        Entity* rootObject = targetScene->m_Entities[0].get();

        // 이 배치가 만든 슬롯만 골라 지운다 — 배치 밖 항목(예: 이전에 붙은 DDOL)은
        // 건드리지 않는다. 조건부 일괄 삭제라 AttachChildIndex/DetachChildIndex
        // 정본 API로는 1:1 대체가 안 돼(SceneGraphRedesignPlan 트랙 E2 배선 여지로
        // 남겨 둔다) 여기만 필드에 직접 접근한다.
        std::vector<Entity::Index> retainedRootChildren = rootObject->GetChildrenIndices();
        std::erase_if(retainedRootChildren, [&](Entity::Index idx) { return batchSlots.contains(idx); });
        rootObject->SetChildrenIndices(std::move(retainedRootChildren));

        for (auto& entry : batch)
        {
            Entity* obj = entry.object;
            if (!obj || obj->m_index == 0) continue;

            // ★ 무효 부모(파일의 m_parentIndex: -1)뿐 아니라 **명시적으로 루트를
            // 가리키는 부모(슬롯 0)**도 루트의 자식이다.
            //
            // 예전에는 IsInvalidIndex만 봤다. 그런데 바로 위 erase_if가 이 배치의
            // 슬롯을 루트 children에서 **전부** 지운 뒤라, m_parentIndex가 0으로
            // 저장된 오브젝트는 지워지기만 하고 다시 얹히지 않았다 — 부모는 루트를
            // 가리키는데 루트의 목록엔 없는, 이 블록의 주석이 "자연히 치유된다"고
            // 적어 둔 바로 그 비대칭이 오히려 여기서 만들어졌다.
            //
            // AllUpdateWorldMatrix는 m_Entities[0]->m_childrenIndices에서만
            // 내려가므로, 그 오브젝트와 그 아래 서브트리 전체가 월드 행렬 갱신
            // 순회에서 통째로 빠진다. 조용하다 — 에러도 경고도 없다.
            //
            // 실측(Test1.creator): 루트 children이 [1,2,3]으로 저장돼 있는데 로드 뒤
            // [1,2]가 되고, m_parentIndex: 0으로 저장된 Gunner_F_Mythic(idx=3)과 그
            // 아래 뼈 61개가 한 번도 순회되지 않았다. 그래서 Bone 분기가 아예 안 돌아
            // BoneComponent::m_boneIndex가 영원히 -1이었다(FindBone은 이름을 제대로
            // 찾는다 — 따로 확인함, scene.bonedump).
            const Entity::Index parentIndex = obj->GetParentIndex();
            if (Entity::IsInvalidIndex(parentIndex)
                || Entity::kSceneRootIndex == parentIndex)
            {
                // 계층 쓰기 정본 API — 중복 검사는 AttachChildIndex 내부에서 한다.
                rootObject->AttachChildIndex(obj->m_index);
            }
        }
    }

	// DDOL 표시는 파일 인덱스 DTO → Store 슬롯 인덱스 리맵과 루트 children
	// 재구성이 모두 끝난 뒤 수행한다.
	for (const auto& entry : batch)
	{
		if (entry.makeDontDestroy && entry.object)
			Object::SetDontDestroyOnLoad(entry.object);
	}
}
