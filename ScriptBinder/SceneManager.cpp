#include "SceneManager.h"
#include "Scene.h"
#include "Object.h"
#include "FileIO.h"
#include "RegisterReflect.h"

void SceneManager::ManagerInitialize()
{
    RegisterReflect();
}

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
	try
	{
        MetaYml::Node sceneNode = MetaYml::LoadFile("TestScene.yml");
        if (m_activeScene)
        {
			delete m_activeScene;
        }
		m_activeScene = Scene::LoadScene(name);

        //auto& gameObjects = m_activeScene->m_SceneObjects;
        for (const auto& objNode : sceneNode["m_SceneObjects"])
        {
			const Meta::Type* type = Meta::ExtractTypeFromYAML(objNode);
			if (!type)
			{
				Debug->LogError("Failed to extract type from YAML node.");
				continue;
			}

            if (type->typeID == TypeTrait::GUIDCreator::GetTypeID<GameObject>())
            {
				auto obj = m_activeScene->LoadGameObject(
                    objNode["m_instanceID"].as<size_t>(), 
                    objNode["m_name"].as<std::string>(), 
                    GameObject::Type::Empty, 
                    objNode["m_parentIndex"].as<GameObject::Index>()
                );

                if(obj)
                {
                    Deserialize(obj.get(), objNode);
                }

                //obj->m_transform.position = Meta::YamlNodeToVector4(objNode["m_transform"]["position"]);
				//obj->m_transform.rotation = Meta::YamlNodeToVector4(objNode["m_transform"]["rotation"]);
				//obj->m_transform.scale = Meta::YamlNodeToVector4(objNode["m_transform"]["scale"]);
				//obj->m_index = objNode["m_index"].as<GameObject::Index>();
				//obj->m_rootIndex = objNode["m_rootIndex"].as<GameObject::Index>();
				//obj->m_childrenIndices = objNode["m_childrenIndices"].as<std::vector<GameObject::Index>>();
				//if (objNode["m_components"])
				//{
				//	for (const auto& componentNode : objNode["m_components"])
				//	{
				//		const Meta::Type& componentType = *Meta::ExtractTypeFromYAML(componentNode);
				//		obj->AddComponent(componentType);
				//	}
				//}
            }
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
