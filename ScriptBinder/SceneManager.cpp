#include "SceneManager.h"
#include "Scene.h"
#include "Object.h"
#include "FileIO.h"
#include "DataSystem.h"
#include "RegisterReflect.def"

void SceneManager::ManagerInitialize()
{
    REFLECTION_REGISTER_EXECUTE()
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
			Scene* swapScene = m_activeScene;
			m_activeScene = nullptr;
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

void SceneManager::DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode)
{
    if (type->typeID == TypeTrait::GUIDCreator::GetTypeID<GameObject>())
    {
        auto obj = m_activeScene->LoadGameObject(
            itNode["m_instanceID"].as<size_t>(),
            itNode["m_name"].as<std::string>(),
            GameObject::Type::Empty,
            itNode["m_parentIndex"].as<GameObject::Index>()
        ).get();

        if (obj)
        {
            Deserialize(obj, itNode);
        }

        if (itNode["m_components"])
        {
            for (const auto& componentNode : itNode["m_components"])
            {
				DesirealizeComponent(type, obj, componentNode);
            }
        }
    }
}

void SceneManager::DesirealizeComponent(const Meta::Type* type, GameObject* obj, const MetaYml::detail::iterator_value& itNode)
{
    const Meta::Type* componentType = Meta::ExtractTypeFromYAML(itNode);
    if (nullptr == componentType)
    {
        return;
    }
    
    auto component = obj->AddComponent((*componentType)).get();
    if (component)
    {
        using namespace TypeTrait;
        if (componentType->typeID == GUIDCreator::GetTypeID<MeshRenderer>())
        {
            auto meshRenderer = static_cast<MeshRenderer*>(component);
			Model* model = nullptr;
			if (itNode["m_Material"])
			{
				auto materialNode = itNode["m_Material"];
				FileGuid guid = materialNode["m_fileGuid"].as<std::string>();
                model = DataSystems->LoadModelGUID(guid);
			}
			MetaYml::Node getMeshNode = itNode["m_Mesh"];
			if (model && getMeshNode)
            {
                meshRenderer->m_Material = model->GetMaterial(getMeshNode["m_materialIndex"].as<int>());
                meshRenderer->m_Mesh = model->GetMesh(getMeshNode["m_name"].as<std::string>());
            }
            Deserialize(meshRenderer, itNode);
            meshRenderer->SetEnabled(true);
        }
    }
}
