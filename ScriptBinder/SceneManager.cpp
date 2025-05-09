#include "SceneManager.h"
#include "Scene.h"
#include "Object.h"
#include "FileIO.h"
#include "DataSystem.h"
#include "ComponentFactory.h"
#include "RegisterReflect.def"

void SceneManager::ManagerInitialize()
{
    REFLECTION_REGISTER_EXECUTE();
	ComponentFactorys->Initialize();
}

void SceneManager::Editor()
{
    if(m_isGameStart && !m_isEditorSceneLoaded)
    {
        CreateEditorOnlyPlayScene();
        ScriptManager->UpdateSceneManager(SceneManager::GetInstance().get());
        m_activeScene->Reset();
		m_isEditorSceneLoaded = true;
    }
    else if (!m_isGameStart && m_isEditorSceneLoaded)
    {
        DeleteEditorOnlyPlayScene();
    }

    if (!m_isGameStart)
    {
        ScriptManager->ReloadDynamicLibrary();
		m_activeScene->Awake();
	}
}

void SceneManager::Initialization()
{
	m_activeScene->Awake();
    m_activeScene->OnEnable();
    m_activeScene->Start();
}

void SceneManager::Physics(float deltaSecond)
{
    m_activeScene->FixedUpdate(deltaSecond);
}

void SceneManager::InputEvents(float deltaSecond)
{
    InputEvent.Broadcast(deltaSecond);
}

void SceneManager::GameLogic(float deltaSecond)
{
    m_activeScene->Update(deltaSecond);
    m_activeScene->YieldNull();
    InternalAnimationUpdateEvent.Broadcast(deltaSecond);
    m_activeScene->LateUpdate(deltaSecond);
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
}

void SceneManager::Pausing()
{
}

void SceneManager::DisableOrEnable()
{
    m_activeScene->OnDisable();
    m_activeScene->OnDestroy();
}

void SceneManager::Deccommissioning()
{
    m_activeScene->OnDisable();
    m_activeScene->AllDestroyMark();
    m_activeScene->OnDestroy();
}

Scene* SceneManager::CreateScene(const std::string_view& name)
{
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
    file::path saveSceneFileName = name.data();
	std::ofstream sceneFileOut(saveSceneFileName);
    MetaYml::Node sceneNode{};

    m_activeScene->m_SceneObjects[0]->m_name = saveSceneFileName.stem().string();

    try
    {
        sceneNode = Meta::Serialize(m_activeScene);
    }
	catch (const std::exception& e)
	{
		Debug->LogError(e.what());
		return nullptr;
	}

	sceneFileOut << sceneNode;
}

Scene* SceneManager::LoadScene(const std::string_view& name, bool isAsync)
{
	std::string loadSceneName = name.data();

	try
	{
        MetaYml::Node sceneNode = MetaYml::LoadFile(loadSceneName);
        if (m_activeScene)
        {
			Scene* swapScene = m_activeScene;
			m_activeScene = nullptr;

			std::erase_if(m_scenes, 
                [&](const auto& scene) { return scene == swapScene; });

			delete swapScene;
        }
		m_activeScene = Scene::LoadScene(name);

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

void SceneManager::CreateEditorOnlyPlayScene()
{
    MetaYml::Node sceneNode{};

    try
    {
        //resetSelectedObjectEvent.Broadcast();
        sceneNode = Meta::Serialize(m_activeScene);
		Scene* playScene = Scene::LoadScene("PlayScene");
        m_scenes.push_back(playScene);
        m_EditorSceneIndex = m_activeSceneIndex;
        m_activeSceneIndex = m_scenes.size() - 1;
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
		m_activeScene->AllDestroyMark();
		m_activeScene->OnDisable();
		m_activeScene->OnDestroy();
		m_activeScene = nullptr;
	}

	Scene* swapScene = m_scenes[m_activeSceneIndex];
    std::erase_if(m_scenes,
        [&](const auto& scene) { return scene == swapScene; });
	delete swapScene;
	swapScene = nullptr;

	m_activeSceneIndex = m_EditorSceneIndex;
	m_isEditorSceneLoaded = false;
	activeSceneChangedEvent.Broadcast();
	sceneUnloadedEvent.Broadcast();
	m_activeScene = m_scenes[m_EditorSceneIndex];
}

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
{
    if (type->typeID == TypeTrait::GUIDCreator::GetTypeID<GameObject>())
    {
        auto obj = m_activeScene->LoadGameObject(
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
                    ComponentFactorys->LoadComponent(obj, componentNode);
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
