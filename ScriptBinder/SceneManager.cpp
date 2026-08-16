#include "SceneManager.h"
#include "EngineMode.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Object.h"
// PrefabEditor.h(19번째 줄 아래)는 내부가 DYNAMICCPP_EXPORTS로 통째로 가드돼
// 있어 그 경로로는 프리팹 재연결에 못 쓴다 — Prefab.cpp도 그래서 이 헤더를
// 무가드로 직접 문다(SceneGraphRedesignPlan P2).
#include "PrefabUtility.h"
#include "FileIO.h"
#include "DataSystem.h"
#include "ComponentFactory.h"
#include "RegisterReflect.def"
#include "RegisterReflectManual.h" // CT4: 명시 메타 이전 타입의 등록 (def 스캔 밖)
#include "Profiler.h"
#include "InputActionManager.h"
#include "TagManager.h"
#include "ReflectionRegister.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "Component.h"
#include "TimeSystem.h"
#include "PrefabEditor.h"
#ifndef DYNAMICCPP_EXPORTS
#include "GpuDiagnostics.h"
#include "ScriptComponent.h"
#include "ClrHost.h"

// 예전에는 여기에 SuspendSceneScripts가 있었다. 재생 시작이 에디터 씬을 복제해
// PlayScene을 만들던 시절, 원본과 사본의 스크립트 인스턴스가 둘 다 틱을 받아
// 로직이 두 벌 도는 것을 막는 봉합이었다. 씬을 복제하지 않게 되면서 두 벌이
// 생길 여지 자체가 사라져 걷어냈다.
#endif

namespace
{
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
    void ReconnectPrefabInstance(Scene* scene, GameObject* obj)
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
        if (obj->m_parentIndex != GameObject::INVALID_INDEX)
        {
            if (auto parentObj = scene->TryGetGameObject(obj->m_parentIndex))
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

void SceneManager::SetGameStart(bool isStart)
{
    if (!isStart)
    {
        SetGamePaused(false);
    }

    m_isGameStart = isStart;

#ifndef DYNAMICCPP_EXPORTS
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
#endif
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
    REFLECTION_REGISTER_EXECUTE();
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
        PROFILE_CPU_BEGIN("CreateEditorOnlyPlayScene");
        CreateEditorOnlyPlayScene();
        PROFILE_CPU_END();
        PROFILE_CPU_BEGIN("Reset");
        activeScenePtr->Reset();
        PROFILE_CPU_END();
		m_isEditorSceneLoaded = true;
    }
    else if (!m_isGameStart && m_isEditorSceneLoaded)
    {
        PROFILE_CPU_BEGIN("DeleteEditorOnlyPlayScene");
        DeleteEditorOnlyPlayScene();
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
        std::erase_if(m_dontDestroyOnLoadObjects, [](const std::shared_ptr<Object>& o){ return !o || o->IsDestroyMark(); });
		//m_inputActionManager->ClearActionMaps();  //&&&&&TODO:게임스타트 한번만 초기화하고 다시들어가게
        m_isInitialized = false; // Reset initialization state for editor scene
        activeScenePtr->Awake();
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

    PROFILE_CPU_BEGIN("Awake");
	m_activeScene.load()->Awake();
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("OnEnable");
    m_activeScene.load()->OnEnable();
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("Start");
    m_activeScene.load()->Start();
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
    std::erase_if(m_dontDestroyOnLoadObjects, [](const std::shared_ptr<Object>& o){ return !o || o->IsDestroyMark(); });
}

void SceneManager::Pausing()
{
    if (!m_activeScene) return;
    m_activeScene.load()->UpdateRenderData();
}

void SceneManager::DisableOrEnable()
{
    if (!m_activeScene) return;
    m_activeScene.load()->OnDisable();
    m_activeScene.load()->OnDestroy();
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

    for (auto& scene : m_scenes)
    {
        if (scene)
        {
            scene->OnDisable();
            scene->AllDestroyMark();
            scene->OnDestroy();
        }
    }

    // Destroy and clear DontDestroyOnLoad objects
    for (auto& o : m_dontDestroyOnLoadObjects) if (o) o->Destroy();
    m_dontDestroyOnLoadObjects.clear();

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
        swapScene->OnDisable();
        swapScene->OnDestroy();

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

	std::ofstream sceneFileOut(saveSceneFileName);
    MetaYml::Node sceneNode{};
	MetaYml::Node assetsBundleNode{};

    m_activeScene.load()->m_SceneObjects[0]->m_name = saveSceneFileName.stem().string();
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

            auto gameObject = std::dynamic_pointer_cast<GameObject>(obj);
            if (gameObject)
            {
                dontDestroyOnLoadNode.push_back(Meta::Serialize(gameObject.get()));
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
	std::string loadSceneName = name.data();

	try
	{
        MetaYml::Node sceneNode = MetaYml::LoadFile(loadSceneName);
        Scene* swapScene{};
        if (m_activeScene)
        {
            for(auto& object : m_dontDestroyOnLoadObjects)
            {
				auto go = std::dynamic_pointer_cast<GameObject>(object);
				if (go)
                {
                    m_activeScene.load()->DetachGameObjectHierarchy(go.get());
                }
            }

			swapScene = m_activeScene.load();
            sceneUnloadedEvent.Broadcast();
            m_activeScene.load()->AllDestroyMark();
            m_activeScene.load()->OnDisable();
            m_activeScene.load()->OnDestroy();

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

        // m_SceneObjects와 DontDestroyOnLoadObjects 루프 둘 다 같은 씬(m_activeScene)의
        // 슬롯을 할당하므로 배치 하나를 공유한다 — 파일 인덱스가 두 절 사이를
        // 넘나들며 서로를 참조해도(예: DDOL이 일반 오브젝트를 부모로) 안전하다.
        LoadIndexBatch loadBatch;

        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
            try
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }

                DesirealizeGameObject(type, objNode, &loadBatch);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(std::string("Failed to deserialize GameObject: ") + e.what());
                continue;
			}
        }

        for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
        {
            try
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode, &loadBatch);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(std::string("Failed to deserialize DontDestroyOnLoadObject: ") + e.what());
                continue;
			}
		}

        RemapLoadBatchIndices(m_activeScene.load(), loadBatch);

        // 프리팹 인스턴스 재연결(SceneGraphRedesignPlan P2) — 리매핑 직후, m_SceneObjects·
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
		// 플레이어는 씬이 로드되는 순간이 곧 재생 시작이다 (B0-1: 런타임 모드).
		// "Scene loaded" 로그는 스모크(--smoke)의 판정 마커이기도 하다 — §2.3.
		if (EngineMode::IsPlayer())
		{
			SceneManagers->SetGameStart(true);
			std::cout << "Scene loaded: " << loadSceneName << std::endl;
		}
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
        MetaYml::Node sceneNode = MetaYml::LoadFile(loadSceneName);
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

        // ★ 두 루프가 서로 다른 씬을 타깃으로 한다 — m_SceneObjects는 방금 만든
        // `scene`으로, DontDestroyOnLoadObjects는 (아직 활성화 전인) m_activeScene으로
        // 들어간다(이 함수의 기존 동작을 그대로 유지 — scene.load는 활성 씬을 바꾸지
        // 않는다). 슬롯 인덱스 공간이 서로 다르므로 배치도 둘로 나눈다.
        LoadIndexBatch sceneBatch;
        LoadIndexBatch ddolBatch;

        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }

            DesirealizeGameObject(scene, type, objNode, &sceneBatch);
        }

        for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }
            DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode, &ddolBatch);
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

            for (const auto& objNode : sceneNode["m_SceneObjects"])
            {
                try
                {
                    const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                    if (!type)
                    {
                        Debug->LogError("Failed to extract type from YAML node.");
                        continue;
                    }

                    DesirealizeGameObject(newScene, type, objNode, &loadBatch);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(std::string("Failed to deserialize GameObject: ") + e.what());
                    continue;
                }
            }

            for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
            {
                try
                {
                    const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                    if (!type)
                    {
                        Debug->LogError("Failed to extract type from YAML node.");
                        continue;
                    }
                    DesirealizeDontDestroyOnLoadObjects(newScene, type, objNode, &loadBatch);
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
            // 타깃으로 한다 — m_SceneObjects는 newScene, DontDestroyOnLoadObjects는
            // (아직 활성화 전인) m_activeScene. 기존 동작 그대로 유지하고 배치만 나눈다.
            LoadIndexBatch sceneBatch;
            LoadIndexBatch ddolBatch;

            for (const auto& objNode : sceneNode["m_SceneObjects"])
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type) {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeGameObject(newScene, type, objNode, &sceneBatch);
            }

            for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode, &ddolBatch);
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
            oldScene->ResetSelectedSceneObject();

            for (auto& object : m_dontDestroyOnLoadObjects)
            {
                auto go = std::dynamic_pointer_cast<GameObject>(object);
                if (go)
                {
                    m_activeScene.load()->DetachGameObjectHierarchy(go.get());
                }
            }

            UIManagers->SelectUI.reset();
            sceneUnloadedEvent.Broadcast();
            m_activeScene.load()->AllDestroyMark();
            m_activeScene.load()->OnDisable();
            m_activeScene.load()->OnDestroy();

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

#ifndef DYNAMICCPP_EXPORTS
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
#endif
    }
}

bool SceneManager::IsSceneLoading() const
{
    return m_sceneToActivate != nullptr;
}

void SceneManager::AddDontDestroyOnLoad(std::shared_ptr<Object> objPtr)
{
    if (objPtr)
    {
        m_dontDestroyOnLoadObjects.push_back(objPtr);
    }
}

void SceneManager::RemoveDontDestroyOnLoad(std::shared_ptr<Object> objPtr)
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

    // DDOL 루트들을 모아 한 번에 부착(서브트리 포함).
    std::vector<std::shared_ptr<GameObject>> roots;
    roots.reserve(m_dontDestroyOnLoadObjects.size());
    for (auto& obj : m_dontDestroyOnLoadObjects)
    {
        if (auto go = std::dynamic_pointer_cast<GameObject>(obj))
        {
            roots.push_back(go);
        }
    }

    // 씬 공식 API로 부착(유니크 네임/Tag/Layer/루트 children/Transform 부모 세팅 포함)
    auto remap = scene->AttachExistingGameObjectHierarchy(roots);
    (void)remap;

    // (D) 루트 규약 통일: INVALID_INDEX(= -1)일 때 부모 없음으로 설정
    for (auto& gameObject : roots)
    {
        if (!gameObject) continue;
        // 역직렬화로 들어온 부모 인덱스를 Transform에 동기화한다.
        gameObject->SetParentIndex(gameObject->m_parentIndex);

        // (D) 루트 판정: 0이 아닌 INVALID_INDEX를 루트로 취급
        if (gameObject->m_parentIndex == GameObject::INVALID_INDEX)
        {
            // 계층 쓰기 정본 API(SceneGraphRedesignPlan 트랙 E2) — 중복 검사는
            // AttachChildIndex 내부에서 한다.
            scene->m_SceneObjects[0]->AttachChildIndex(gameObject->m_index);
        }
    }

    // 컴포넌트 생명주기 재등록.
    //
    // DDOL 오브젝트는 씬을 건너 살아남으므로 새 씬의 디스패치 대상에 다시 넣어야 한다.
    // 이 경로를 빠뜨리면 씬 전환 후 DDOL 오브젝트만 조용히 틱을 못 받는다 —
    // 증상이 '가끔 안 움직인다'라 원인을 짚기 어려운 종류다.
    for (auto& obj : m_dontDestroyOnLoadObjects)
    {
        auto go = std::dynamic_pointer_cast<GameObject>(obj);
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

void SceneManager::VolumeProfileApply()
{
	m_volumeProfileApply = true;
}

void SceneManager::CreateEditorOnlyPlayScene()
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
        PROFILE_CPU_BEGIN("UndoCommandManager::ClearGameMode");
        Meta::UndoCommandManager->ClearGameMode();
		Meta::UndoCommandManager->Clear();
        PROFILE_CPU_END();

        PROFILE_CPU_BEGIN("Serialize");
        // 편집 중이던 최신 값이 담기도록 직렬화한다. 스크립트를 재우지 않는
        // 이유는 사본이 없어 두 벌이 생기지 않기 때문이다 — 지금 씬의 인스턴스가
        // 그대로 플레이 인스턴스가 된다.
        m_editorSceneBackup = Meta::Serialize(m_activeScene.load());
        PROFILE_CPU_END();
        resourceTrimEvent.Broadcast();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return;
    }

    // Simulating 전이(SceneGraphRedesignPlan §4 트랙 L1) — 씬을 복제하지 않으므로
    // 이 시점의 오브젝트 전원이 곧 플레이 인스턴스다. OnBeginSimulation 자체는
    // 여기서 부르지 않는다 — Start()는 이미 매 프레임 드는 pendingAwake/Start
    // 드레인이 State_StartCalled 가드로 정확히 한 번만 부르고 있어(에디터 틱도
    // 예외가 아니다), 여기서 다시 부르면 그 가드를 건너뛰고 두 번 불린다.
    // phase 필드는 그 드레인과 무관하게 상태 기계 자체를 정확히 유지하기 위한
    // 부기다.
    for (auto& obj : m_activeScene.load()->m_SceneObjects)
    {
        if (obj) obj->m_scenePhase = ScenePhase::Simulating;
    }
}

void SceneManager::DeleteEditorOnlyPlayScene()
{
    // 재생 정지. 같은 Scene 객체를 비우고 백업으로 되채운다.
    Scene* scene = m_activeScene.load();
    if (nullptr == scene)
    {
        m_isEditorSceneLoaded = false;
        return;
    }

    if (!m_editorSceneBackup || !m_editorSceneBackup["m_SceneObjects"])
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
    scene->OnDisable();
    scene->OnDestroy();

    // 파괴 뒤에 던진다(ClrHost.h 선언 주석 참고). 이 경로는 특히 DDOL이 살아남는
    // 자리라, 파괴 전에 부르는 형태였다면 재생 종료마다 DDOL 스크립트가 죽었을 것이다.
    ClrHost::Get().NotifySceneUnload();

    // 살아남은 오브젝트(DontDestroyOnLoad)는 백업에도 실려 있다. 그대로
    // 역직렬화하면 같은 객체가 한 벌 더 생기므로 instanceID로 걸러낸다.
    // DDOL을 파괴하지 않는 것은 현재 엔진의 정책을 그대로 둔 것이다 —
    // 유니티는 재생 종료 때 DDOL도 버리지만, 그 정책 변경은 이 수정의
    // 범위 밖이고 회귀 범위가 훨씬 넓다.
    std::unordered_set<size_t> survivingIds;
    for (const auto& object : scene->m_SceneObjects)
    {
        if (object) survivingIds.insert(object->GetInstanceID());
    }

    try
    {
        PROFILE_CPU_BEGIN("RestoreEditorScene");
        LoadIndexBatch loadBatch;
        for (const auto& objNode : m_editorSceneBackup["m_SceneObjects"])
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

            DesirealizeGameObject(type, objNode, &loadBatch);
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
        for (auto& obj : scene->m_SceneObjects)
        {
            if (obj) obj->m_scenePhase = ScenePhase::InScene;
        }

        scene->AllUpdateWorldMatrix();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
    }

    m_editorSceneBackup = MetaYml::Node{};

    activeSceneChangedEvent.Broadcast();
    sceneLoadedEvent.Broadcast();

	m_isEditorSceneLoaded = false;
}

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode, LoadIndexBatch* batch)
{
    if (type->typeID == type_guid(GameObject))
    {
        // 프리팹 재연결(P-a)은 더 이상 여기서 오브젝트 하나씩 하지 않는다 — 이 배치가
        // 로드된 뒤 RemapLoadBatchIndices까지 끝나야 m_parentIndex가 슬롯 인덱스로
        // 확정되므로, 재연결은 로더 각 호출부가 배치 전체를 한 번에 훑는다
        // (ReconnectPrefabInstance, SceneGraphRedesignPlan P2).

        auto obj = m_activeScene.load()->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — Deserialize가
            // YAML의 m_index로 obj->m_index를 덮어쓰는데, 부분 편집·병합된 씬 파일은 이
            // 값이 실제 슬롯 위치와 어긋날 수 있다. LoadGameObject가 갓 부여한 값(=실제
            // 슬롯 위치)을 미리 붙잡아 뒀다가, 덮어써진 뒤 어긋나면 정정한다.
            //
            // 같은 배치의 다른 오브젝트가 든 m_parentIndex/m_childrenIndices/m_rootIndex는
            // 여전히 파일 인덱스 스킴이다 — 이 함수만으로는 고칠 수 없다(아직 로드되지
            // 않은 오브젝트를 참조할 수 있으므로). 그래서 여기서는 정정만 하고, 파일
            // 값을 배치 버퍼에 남겨 로드 루프가 끝난 뒤 RemapLoadBatchIndices가 한 번에
            // 고치게 한다.
            const GameObject::Index actualSlotIndex = obj->m_index;

            Meta::Deserialize(obj, itNode);

            const GameObject::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
                batch->push_back({ obj, fileIndex });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

        }

        if (itNode["m_components"])
        {
            for (const auto& componentNode : itNode["m_components"])
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, componentNode, m_isGameStart);
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

void SceneManager::DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode, LoadIndexBatch* batch)
{
    if (type->typeID == type_guid(GameObject))
    {
        // 프리팹 재연결(P-a)은 더 이상 여기서 오브젝트 하나씩 하지 않는다 — 이유는
        // 위 오버로드 주석 참고(ReconnectPrefabInstance, SceneGraphRedesignPlan P2).

        auto obj = targetScene->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — 위와 동일한
            // 이유로 여기서도 검증한다(DDOL이 아닌 일반 씬 오브젝트 로드 경로). 배치
            // 버퍼에 파일 인덱스를 남기는 이유는 위 오버로드 주석 참고.
            const GameObject::Index actualSlotIndex = obj->m_index;

            Meta::Deserialize(obj, itNode);

            const GameObject::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
                batch->push_back({ obj, fileIndex });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

        }

        if (itNode["m_components"])
        {
            for (const auto& componentNode : itNode["m_components"])
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, componentNode, m_isGameStart);
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

void SceneManager::DesirealizeDontDestroyOnLoadObjects(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode, LoadIndexBatch* batch)
{
    if (type->typeID == type_guid(GameObject))
    {
        auto it = std::find_if(m_dontDestroyOnLoadObjects.begin(), m_dontDestroyOnLoadObjects.end(),
			[&](const auto& obj) { return obj->GetInstanceID() == itNode["m_instanceID"].as<size_t>(); });
        if(it != m_dontDestroyOnLoadObjects.end())
        {
            Debug->LogWarning("Object with instance ID " + std::to_string(itNode["m_instanceID"].as<size_t>()) + " already exists in DontDestroyOnLoad.");
            return; // Object already exists, skip deserialization
		}

        auto obj = targetScene->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();
        if (obj)
        {
            // "m_index == 벡터 위치" 불변식 검증(SceneGraphRedesignPlan §5) — DDOL 로드
            // 경로도 예외가 아니다. 배치 버퍼에 파일 인덱스를 남기는 이유는
            // DesirealizeGameObject 오버로드 주석 참고.
            const GameObject::Index actualSlotIndex = obj->m_index;

            Meta::Deserialize(obj, itNode);

            const GameObject::Index fileIndex = obj->m_index;
            if (fileIndex != actualSlotIndex)
            {
                obj->m_index = actualSlotIndex;
            }

            if (batch)
            {
                batch->push_back({ obj, fileIndex });
            }

            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }
            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }
        }
        if (itNode["m_components"])
        {
            for (const auto& componentNode : itNode["m_components"])
            {
                try
                {
                    ComponentFactorys->LoadComponent(obj, componentNode, m_isGameStart);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(e.what());
                    continue;
                }
            }
        }

        Object::SetDontDestroyOnLoad(obj);
	}
}

void SceneManager::RemapLoadBatchIndices(Scene* targetScene, LoadIndexBatch& batch)
{
    if (!targetScene || batch.empty()) return;

    // 파일 인덱스 → 실제 슬롯 인덱스. 이 배치에서 막 로드된 오브젝트만 담는다 —
    // 이전에 붙어 있던 DDOL이나 다른 배치의 오브젝트는 여기서 다루지 않는다
    // (호출부가 로더/타깃 씬 단위로 배치를 나눠 넘긴다).
    std::unordered_map<GameObject::Index, GameObject::Index> fileToSlot;
    fileToSlot.reserve(batch.size() + 1);
    std::unordered_set<GameObject::Index> batchSlots;
    batchSlots.reserve(batch.size());

    // 합성 루트(슬롯 0)는 씬이 서 있는 동안 절대 재할당되지 않는다(Scene::ReleaseSlot의
    // "index==0은 해제하지 않는다" 참고) — 파일에 적힌 루트의 m_index도 항상 0이다.
    // 이 배치에 루트 자신의 로드 엔트리가 없어도(예: DeleteEditorOnlyPlayScene 복원은
    // 살아남은 루트를 다시 로드하지 않고 건너뛴다) "부모가 루트"를 가리키는 흔한
    // 참조가 데이터 오염으로 오탐되지 않도록 미리 채워 둔다.
    if (!targetScene->m_SceneObjects.empty() && targetScene->m_SceneObjects[0])
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
    const auto remapOrLog = [&](GameObject::Index fileIdx, GameObject* owner, const char* label) -> GameObject::Index
    {
        auto foundIt = fileToSlot.find(fileIdx);
        if (foundIt != fileToSlot.end())
        {
            return foundIt->second;
        }

        Debug->LogError("[Scene] '" + targetScene->GetSceneName().ToString() + "' GameObject '"
            + (owner ? owner->m_name.ToString() : std::string("?")) + "'의 " + label
            + " 참조(파일 인덱스 " + std::to_string(fileIdx) + ")가 이 배치 안에 없습니다 — 데이터 오염 가능성.");
        return GameObject::INVALID_INDEX;
    };

    for (auto& entry : batch)
    {
        GameObject* obj = entry.object;
        if (!obj) continue;

        // m_parentIndex: 못 찾으면 INVALID_INDEX로 남겨 루트로 편입한다(기존
        // "부모 미지정=루트" 관례 — Scene::CreateGameObject/AttachExistingGameObject의
        // 폴백과 동일). 씬 합성 루트(0) 자신은 원래도 무효 부모다.
        if (GameObject::IsValidIndex(obj->m_parentIndex))
        {
            obj->m_parentIndex = remapOrLog(obj->m_parentIndex, obj, "부모(m_parentIndex)");
        }

        // m_childrenIndices: 합성 루트(0)는 아래에서 부모 포인터 기준으로 통째로
        // 재구성하므로 여기서는 건드리지 않는다 — 두 출처가 섞이면(파일이 적어 둔
        // children 목록 vs 자식들의 실제 parentIndex) 어느 쪽이 진실인지 알 수 없다.
        if (obj->m_index != 0)
        {
            std::vector<GameObject::Index> remappedChildren;
            remappedChildren.reserve(obj->m_childrenIndices.size());
            for (GameObject::Index childFileIdx : obj->m_childrenIndices)
            {
                if (!GameObject::IsValidIndex(childFileIdx)) continue;

                GameObject::Index remapped = remapOrLog(childFileIdx, obj, "자식(m_childrenIndices)");
                if (GameObject::IsValidIndex(remapped))
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
        // 명시적으로 이 값을 쓰고, UpdateModelRecursive의 Bone 분기는 TryGetGameObject가
        // nullptr을 돌려주면 조용히 건너뛴다) — 건드리지 않는다. 유효한 파일 인덱스인데
        // 배치 안에 없을 때만 진짜 오염이므로, 그때만 자기 자신으로 대체한다(체인이
        // 끊기는 것보다 안전한 폴백).
        if (GameObject::IsValidIndex(obj->m_rootIndex))
        {
            GameObject::Index remapped = remapOrLog(obj->m_rootIndex, obj, "루트 뼈(m_rootIndex)");
            obj->SetRootIndex(GameObject::IsValidIndex(remapped) ? remapped : obj->m_index);
        }
    }

    // 합성 루트(0)의 children을 배치 기준으로 재구성한다. 진실의 원천은 자식들의
    // 최종 m_parentIndex다(위 루프에서 이미 슬롯 인덱스로 확정됐다) — 루트 자신이
    // 파일에서 읽어 온 children 목록은 신뢰하지 않는다. 그래야 "부모는 루트를
    // 가리키는데 루트의 목록엔 없다" 같은 비대칭 오염도 자연히 치유된다.
    //
    // LoadGameObject는 CreateGameObject/AddGameObject와 달리 부모의 children에
    // 자동으로 얹지 않으므로(Scene::LoadGameObject 참고), 여기서 지우고 다시
    // 채워도 이 배치가 로드되는 동안 다른 경로가 끼워 넣은 값과 섞이지 않는다.
    if (!targetScene->m_SceneObjects.empty() && targetScene->m_SceneObjects[0])
    {
        GameObject* rootObject = targetScene->m_SceneObjects[0].get();

        // 이 배치가 만든 슬롯만 골라 지운다 — 배치 밖 항목(예: 이전에 붙은 DDOL)은
        // 건드리지 않는다. 조건부 일괄 삭제라 AttachChildIndex/DetachChildIndex
        // 정본 API로는 1:1 대체가 안 돼(SceneGraphRedesignPlan 트랙 E2 배선 여지로
        // 남겨 둔다) 여기만 필드에 직접 접근한다.
        std::erase_if(rootObject->m_childrenIndices, [&](GameObject::Index idx) { return batchSlots.contains(idx); });

        for (auto& entry : batch)
        {
            GameObject* obj = entry.object;
            if (!obj || obj->m_index == 0) continue;

            if (GameObject::IsInvalidIndex(obj->m_parentIndex))
            {
                // 계층 쓰기 정본 API — 중복 검사는 AttachChildIndex 내부에서 한다.
                rootObject->AttachChildIndex(obj->m_index);
            }
        }
    }
}
