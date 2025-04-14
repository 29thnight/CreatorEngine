#include "Scene.h"
#include "HotLoadSystem.h"
#include "GameObjectPool.h"

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    std::string uniqueName = GenerateUniqueGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);

	m_SceneObjects.push_back(sceneObject);

	const_cast<GameObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

	return sceneObject;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string_view& name, GameObject::Type type, GameObject::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }
    
    if (parentIndex >= m_SceneObjects.size())
    {
        parentIndex = 0;
    }

    std::string uniqueName = GenerateUniqueGameObjectName(name);

	GameObject::Index index = m_SceneObjects.size();
    auto ptr = ObjectPool::Allocate<GameObject>(uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        return nullptr;
    }

    std::shared_ptr<GameObject> newObj(ptr, [&](GameObject* obj)
    {
        if (obj)
        {
            ObjectPool::Deallocate(obj);
        }
    });

	m_SceneObjects.push_back(newObj);
	auto parentObj = GetGameObject(parentIndex);
	if (parentObj->m_index != index)
	{
		parentObj->m_childrenIndices.push_back(index);
	}

	return m_SceneObjects[index];
}

std::shared_ptr<GameObject> Scene::GetGameObject(GameObject::Index index)
{
	if (index < m_SceneObjects.size())
	{
		return m_SceneObjects[index];
	}
	return m_SceneObjects[0];
}

std::shared_ptr<GameObject> Scene::GetGameObject(const std::string_view& name)
{
	HashingString hashedName(name.data());
	for (auto& obj : m_SceneObjects)
	{
		if (obj->GetHashedName() == hashedName)
		{
			return obj;
		}
	}
	return nullptr;
}

void Scene::Reset()
{
}

void Scene::Awake()
{
    AwakeEvent.Broadcast();
}

void Scene::OnEnable()
{
    OnEnableEvent.Broadcast();
}

void Scene::Start()
{
    StartEvent.Broadcast();
}

void Scene::FixedUpdate(float deltaSecond)
{
    FixedUpdateEvent.Broadcast(deltaSecond);
}

void Scene::OnTriggerEnter(ICollider* collider)
{
    OnTriggerEnterEvent.Broadcast(collider);
}

void Scene::OnTriggerStay(ICollider* collider)
{
    OnTriggerStayEvent.Broadcast(collider);
}

void Scene::OnTriggerExit(ICollider* collider)
{
    OnTriggerExitEvent.Broadcast(collider);
}

void Scene::OnCollisionEnter(ICollider* collider)
{
    OnCollisionEnterEvent.Broadcast(collider);
}

void Scene::OnCollisionStay(ICollider* collider)
{
    OnCollisionStayEvent.Broadcast(collider);
}

void Scene::OnCollisionExit(ICollider* collider)
{
    OnCollisionExitEvent.Broadcast(collider);
}

void Scene::Update(float deltaSecond)
{
    UpdateEvent.Broadcast(deltaSecond);
}

void Scene::YieldNull()
{
	ScriptManager->ReplaceScriptComponent();
}

void Scene::LateUpdate(float deltaSecond)
{
    LateUpdateEvent.Broadcast(deltaSecond);
}

void Scene::OnDisable()
{
    OnDisableEvent.Broadcast();
    for (auto& obj : m_SceneObjects)
    {
        if (obj->IsDestroyMark())
        {
            obj.reset();
        }
    }

    std::erase_if(m_SceneObjects, [](const auto& obj)
    {
        return nullptr == obj;
    });
}

void Scene::OnDestroy()
{
    OnDestroyEvent.Broadcast();
}

