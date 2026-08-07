#include "SceneManager.h"
#include "Scene.h"
#include "Object.h"
#include "FileIO.h"
#include "DataSystem.h"
#include "ComponentFactory.h"
#include "RegisterReflect.def"
#include "CullingManager.h"
#include "Profiler.h"
#include "InputActionManager.h"
#include "NodeFactory.h"
#include "TagManager.h"
#include "ReflectionRegister.h"
#include <algorithm>
#include "Component.h"
#include "TimeSystem.h"
#include "PrefabEditor.h"
#ifndef DYNAMICCPP_EXPORTS
#include "DeviceResources.h"
#include "ScriptComponent.h"
#include "ClrHost.h"

// 예전에는 여기에 SuspendSceneScripts가 있었다. 재생 시작이 에디터 씬을 복제해
// PlayScene을 만들던 시절, 원본과 사본의 스크립트 인스턴스가 둘 다 틱을 받아
// 로직이 두 벌 도는 것을 막는 봉합이었다. 씬을 복제하지 않게 되면서 두 벌이
// 생길 여지 자체가 사라져 걷어냈다.
#endif

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
	//GC_Initialize();
    REFLECTION_REGISTER_EXECUTE();
    ComponentFactorys->Initialize();
	m_threadPool = new ThreadPool;
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
        //GC_FullCollect();
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
        //GC_FullCollect();
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
    m_activeScene.load()->CullMeshData();
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

    Memory::SafeDelete(m_threadPool);

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
            delete scene;
        }
	}

	//GC_Shutdown();
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

        std::erase_if(m_scenes,
            [&](const auto& scene) { return scene == swapScene; });

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
			
            m_activeScene = nullptr;
            
            std::erase_if(m_scenes,
                [&](const auto& scene) { return scene == swapScene; });

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

                DesirealizeGameObject(type, objNode);
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
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(std::string("Failed to deserialize DontDestroyOnLoadObject: ") + e.what());
                continue;
			}
		}

        RebindEventDontDestroyOnLoadObjects(m_activeScene.load());
        m_activeScene.load()->AllUpdateWorldMatrix();

		m_scenes.push_back(m_activeScene);
		m_activeSceneIndex = m_scenes.size() - 1;
		activeSceneChangedEvent.Broadcast();
		sceneLoadedEvent.Broadcast();
#ifdef BUILD_FLAG
        SceneManagers->SetGameStart(true);
                std::cout << "Scene loaded: " << loadSceneName << std::endl;
#endif
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

        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }

            DesirealizeGameObject(scene, type, objNode);
        }

        for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }
            DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode);
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

                    DesirealizeGameObject(newScene, type, objNode);
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
                    DesirealizeDontDestroyOnLoadObjects(newScene, type, objNode);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(std::string("Failed to deserialize DontDestroyOnLoadObject: ") + e.what());
                    continue;
                }
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

            for (const auto& objNode : sceneNode["m_SceneObjects"])
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type) {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeGameObject(newScene, type, objNode);
            }

            for (const auto& objNode : sceneNode["DontDestroyOnLoadObjects"])
            {
                const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
                if (!type)
                {
                    Debug->LogError("Failed to extract type from YAML node.");
                    continue;
                }
                DesirealizeDontDestroyOnLoadObjects(m_activeScene.load(), type, objNode);
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
        if (auto* deviceResources = DirectX11::DeviceResources::GetActive())
        {
            deviceResources->LogLiveObjectDelta("씬 전환 완료");
        }
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
        gameObject->m_transform.SetParentID(GameObject::IsValidIndex(gameObject->m_parentIndex)
            ? gameObject->m_parentIndex
            : GameObject::INVALID_INDEX);

        // (D) 루트 판정: 0이 아닌 INVALID_INDEX를 루트로 취급
        if (gameObject->m_parentIndex == GameObject::INVALID_INDEX)
        {
            auto& rootChildren = scene->m_SceneObjects[0]->m_childrenIndices;
            if (std::find(rootChildren.begin(), rootChildren.end(), gameObject->m_index) == rootChildren.end())
            {
                rootChildren.push_back(gameObject->m_index);
            }
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

            DesirealizeGameObject(type, objNode);
        }
        PROFILE_CPU_END();

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

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
{
    if (type->typeID == type_guid(GameObject))
    {
		//Prefab* m_prefab = nullptr;
  //      if (itNode["m_prefabFileGuid"] && !itNode["m_prefabFileGuid"].IsNull())
  //      {
  //          auto prefabGuid = itNode["m_prefabFileGuid"].as<std::string>();
  //          if (prefabGuid != nullFileGuid)
  //          {
  //              m_prefab = PrefabUtilitys->LoadPrefabGuid(prefabGuid);
  //          }
  //      }

        auto obj = m_activeScene.load()->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            Meta::Deserialize(obj, itNode);
            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

			//if (m_prefab)
   //         {
   //             obj->m_prefab = m_prefab;
			//	PrefabUtilitys->RegisterInstance(obj, m_prefab);
   //         }

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

void SceneManager::DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
{
    if (type->typeID == type_guid(GameObject))
    {
        //Prefab* m_prefab = nullptr;
        //if (itNode["m_prefabFileGuid"] && !itNode["m_prefabFileGuid"].IsNull())
        //{
        //    auto prefabGuid = itNode["m_prefabFileGuid"].as<std::string>();
        //    if (prefabGuid != nullFileGuid)
        //    {
        //        m_prefab = PrefabUtilitys->LoadPrefabGuid(prefabGuid);
        //    }
        //}

        auto obj = targetScene->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            Meta::Deserialize(obj, itNode);
            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
            }

            if (!obj->m_layer.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
            }

            //if (m_prefab)
            //{
            //    obj->m_prefab = m_prefab;
            //    PrefabUtilitys->RegisterInstance(obj, m_prefab);
            //}
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

void SceneManager::DesirealizeDontDestroyOnLoadObjects(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
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
            Meta::Deserialize(obj, itNode);
            if (!obj->m_tag.ToString().empty())
            {
                TagManager::GetInstance()->AddObjectToLayer(obj->m_tag.ToString(), obj);
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
