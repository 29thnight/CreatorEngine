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
#include "TagManager.h"
void SceneManager::ManagerInitialize()
{
    REFLECTION_REGISTER_EXECUTE();
	ComponentFactorys->Initialize();
	m_threadPool = new ThreadPool;
    m_inputActionManager = new InputActionManager();
    InputActionManagers = m_inputActionManager;
	TagManager::GetInstance()->Initialize();
}

void SceneManager::Editor()
{
    PROFILE_CPU_BEGIN("Editor");
    if(m_isGameStart && !m_isEditorSceneLoaded)
    {
        CreateEditorOnlyPlayScene();
        ScriptManager->UpdateSceneManager(SceneManager::GetInstance().get());
        m_activeScene.load()->Reset();
		m_isEditorSceneLoaded = true;
    }
    else if (!m_isGameStart && m_isEditorSceneLoaded)
    {
        DeleteEditorOnlyPlayScene();
    }

    if (!m_isGameStart)
    {
        ScriptManager->ReloadDynamicLibrary();
		m_activeScene.load()->Awake();
	}
    PROFILE_CPU_END();
}

void SceneManager::Initialization()
{
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
	CoroutineManagers->yield_WaitForEndOfFrame();
    endOfFrameEvent.Broadcast();
}

void SceneManager::Pausing()
{
}

void SceneManager::DisableOrEnable()
{
    m_activeScene.load()->OnDisable();
    m_activeScene.load()->OnDestroy();
}

void SceneManager::Decommissioning()
{
    m_activeScene.load()->OnDisable();
    m_activeScene.load()->AllDestroyMark();
    m_activeScene.load()->OnDestroy();
}

Scene* SceneManager::CreateScene(const std::string_view& name)
{
    if (m_activeScene)
    {
        std::erase_if(m_scenes,
            [&](const auto& scene) { return scene == m_activeScene.load(); });

        m_activeScene.load()->AllDestroyMark();
        m_activeScene.load()->OnDisable();
        m_activeScene.load()->OnDestroy();
        m_activeScene = nullptr;
    }

    resourceTrimEvent.Broadcast();
    Scene* allocScene = Scene::CreateNewScene(name);
    if (allocScene)
    {
        m_scenes.push_back(allocScene);
        m_activeScene = allocScene;
        m_activeSceneIndex = m_scenes.size() - 1;
        allocScene->m_buildIndex = m_activeSceneIndex.load();
        activeSceneChangedEvent.Broadcast();
        newSceneCreatedEvent.Broadcast();
    }

    return allocScene;
}

Scene* SceneManager::SaveScene(const std::string_view& name, bool isAsync)
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

	sceneFileOut << sceneNode;

    sceneFileOut.close();
}

Scene* SceneManager::LoadScene(const std::string_view& name, bool isAsync)
{
	std::string loadSceneName = name.data();

	try
	{
        MetaYml::Node sceneNode = MetaYml::LoadFile(loadSceneName);
        Scene* swapScene{};
        if (m_activeScene)
        {
			swapScene = m_activeScene.load();
            swapScene->AllDestroyMark();
            swapScene->OnDisable();
            swapScene->OnDestroy();

			sceneUnloadedEvent.Broadcast();

			m_activeScene = nullptr;

            std::erase_if(m_scenes,
                [&](const auto& scene) { return scene == swapScene; });

            delete swapScene;
        }
		file::path sceneName = name.data();
        resourceTrimEvent.Broadcast();
		m_activeScene = Scene::LoadScene(sceneName.stem().string());

        //if(sceneNode["AssetsBundle"])
        //{
        //    auto assetsBundleNode = sceneNode["AssetsBundle"];
        //    if (assetsBundleNode.IsNull())
        //    {
        //        Debug->LogError("AssetsBundle node is null.");
        //    }
        //    else
        //    {
        //        auto* AssetBundle = &m_activeScene.load()->m_requiredLoadAssetsBundle;
        //        Meta::Deserialize(AssetBundle, assetsBundleNode);
        //    }
        //}

        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
			const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
			if (!type)
			{
				Debug->LogError("Failed to extract type from YAML node.");
				continue;
			}

            DesirealizeGameObject(type, objNode);
        }
        m_activeScene.load()->AllUpdateWorldMatrix();

        if (swapScene)
        {

        }

		m_scenes.push_back(m_activeScene);
		m_activeSceneIndex = m_scenes.size() - 1;

		activeSceneChangedEvent.Broadcast();
		sceneLoadedEvent.Broadcast();
	}
	catch (const std::exception& e)
	{
		Debug->LogError(e.what());
		return nullptr;
	}
	return m_activeScene;
}

void SceneManager::AddDontDestroyOnLoad(Object* objPtr)
{
    if (objPtr)
    {
        m_dontDestroyOnLoadObjects.push_back(objPtr);
    }
}

std::vector<MeshRenderer*> SceneManager::GetAllMeshRenderers() const
{
	return m_activeScene.load()->m_allMeshRenderers;
}

void SceneManager::CreateEditorOnlyPlayScene()
{
    MetaYml::Node sceneNode{};

    try
    {
        //resetSelectedObjectEvent.Broadcast();
        sceneNode = Meta::Serialize(m_activeScene.load());
		resourceTrimEvent.Broadcast();
		Scene* playScene = Scene::LoadScene("PlayScene");
        m_scenes.push_back(playScene);
        m_EditorSceneIndex = m_activeSceneIndex;
        m_activeSceneIndex = m_scenes.size() - 1;

        sceneUnloadedEvent.Broadcast();
        
        m_activeScene = playScene;

        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
            const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
            if (!type)
            {
                Debug->LogError("Failed to extract type from YAML node.");
                continue;
            }

            DesirealizeGameObject(type, objNode);
        }
        m_activeScene.load()->AllUpdateWorldMatrix();

		activeSceneChangedEvent.Broadcast();
		sceneLoadedEvent.Broadcast();
    }
    catch (const std::exception& e)
    {
        Debug->LogError(e.what());
        return;
    }
}

void SceneManager::DeleteEditorOnlyPlayScene()
{
	if (m_activeScene)
	{
        resetSelectedObjectEvent.Broadcast();
		m_activeScene.load()->AllDestroyMark();
		m_activeScene.load()->OnDisable();
		m_activeScene.load()->OnDestroy();
        sceneUnloadedEvent.Broadcast();
		m_activeScene = nullptr;
	}

	Scene* swapScene = m_scenes[m_activeSceneIndex];

    std::erase_if(m_scenes,
        [&](const auto& scene) { return scene == swapScene; });
	
    delete swapScene;
	swapScene = nullptr;

	m_activeSceneIndex = m_EditorSceneIndex;
	m_activeScene = m_scenes[m_EditorSceneIndex];
	activeSceneChangedEvent.Broadcast();

	m_isEditorSceneLoaded = false;
}

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
{
    if (type->typeID == TypeTrait::GUIDCreator::GetTypeID<GameObject>())
    {
        auto obj = m_activeScene.load()->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObjectType::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            Meta::Deserialize(obj, itNode);
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
