#include "SceneManager.h"
#include "Scene.h"
#include "Object.h"
#include "FileIO.h"

void SceneManager::Editor()
{
    m_activeScene->Reset();
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

void SceneManager::GUIRendering()
{
    GUIRenderingEvent.Broadcast();
}

void SceneManager::EndOfFrame()
{
}

void SceneManager::Pausing()
{
}

void SceneManager::DisableOrEnable()
{
    m_activeScene->OnDisable();
}

void SceneManager::Deccommissioning()
{
    m_activeScene->OnDisable();
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
	std::ofstream sceneFileOut("TestScene.yml");
    MetaYml::Node sceneNode{};

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
    return nullptr;
}

void SceneManager::AddDontDestroyOnLoad(Object* objPtr)
{
    if (objPtr)
    {
        m_dontDestroyOnLoadObjects.push_back(objPtr);
    }
}
