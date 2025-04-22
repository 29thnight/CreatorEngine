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

std::shared_ptr<GameObject> Scene::LoadGameObject(size_t instanceID, const std::string_view& name, GameObject::Type type, GameObject::Index parentIndex)
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

void Scene::DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
	if (nullptr == sceneObject)
	{
		return;
	}

	RemoveGameObjectName(sceneObject->GetHashedName().ToString());

	sceneObject->Destroy();
    for (auto& childIndex : sceneObject->m_childrenIndices)
    {
		DestroyGameObject(childIndex);
    }
}

void Scene::DestroyGameObject(GameObject::Index index)
{
	if (index < m_SceneObjects.size())
	{
		auto obj = m_SceneObjects[index];
		if (nullptr != obj)
		{
            RemoveGameObjectName(obj->GetHashedName().ToString());
			obj->Destroy();

			for (auto& childIndex : obj->m_childrenIndices)
			{
				DestroyGameObject(childIndex);
			}
		}
	}
	else
	{
		return;
	}
}

void Scene::Reset()
{
    //칠게 있나?
    ScriptManager->ReplaceScriptComponent();
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
	CoroutineManagers->yield_WaitForFixedUpdate();
	// Internal Physics Update 작성
	// OnTriggerEvent.Broadcast(); 작성
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
	CoroutineManagers->yield_Null();
	CoroutineManagers->yield_WaitForSeconds();
	CoroutineManagers->yield_OtherEvent();
	CoroutineManagers->yield_StartCoroutine();
}

void Scene::LateUpdate(float deltaSecond)
{
    LateUpdateEvent.Broadcast(deltaSecond);
}

void Scene::OnDisable()
{
    OnDisableEvent.Broadcast();
    DestroyGameObjects();
}

void Scene::OnDestroy()
{
    OnDestroyEvent.Broadcast();

}

void Scene::DestroyGameObjects()
{
    std::unordered_set<uint32_t> deletedIndices;
    for (const auto& obj : m_SceneObjects)
    {
        if (obj && obj->IsDestroyMark())
            deletedIndices.insert(obj->m_index);
    }

	if (deletedIndices.empty())
		return;

    for (const auto& obj : m_SceneObjects)
    {
        if (obj && deletedIndices.contains(obj->m_index))
        {
            for (auto childIdx : obj->m_childrenIndices)
            {
                if (GameObject::IsValidIndex(childIdx) &&
                    childIdx < m_SceneObjects.size() &&
                    m_SceneObjects[childIdx])
                {
                    m_SceneObjects[childIdx]->m_parentIndex = GameObject::INVALID_INDEX;
                }
            }
        }
    }

    for (auto& obj : m_SceneObjects)
    {
        if (obj && deletedIndices.contains(obj->m_index))
        {
            obj->m_childrenIndices.clear();
            obj.reset();
        }
    }
    std::erase_if(m_SceneObjects, [](const auto& obj) { return obj == nullptr; });

    std::unordered_map<uint32_t, uint32_t> indexMap;
    for (uint32_t i = 0; i < m_SceneObjects.size(); ++i)
    {
        indexMap[m_SceneObjects[i]->m_index] = i;
    }

    for (auto& obj : m_SceneObjects)
    {
        uint32_t oldIndex = obj->m_index;

        if (indexMap.contains(obj->m_parentIndex))
            obj->m_parentIndex = indexMap[obj->m_parentIndex];
        else
            obj->m_parentIndex = GameObject::INVALID_INDEX;

        for (auto& childIndex : obj->m_childrenIndices)
        {
            if (indexMap.contains(childIndex))
                childIndex = indexMap[childIndex];
            else
                childIndex = GameObject::INVALID_INDEX;
        }

        std::erase_if(obj->m_childrenIndices, GameObject::IsInvalidIndex);

        obj->m_index = indexMap[oldIndex];
    }
}

