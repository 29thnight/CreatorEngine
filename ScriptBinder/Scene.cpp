#include "Scene.h"
#include "HotLoadSystem.h"
#include "GameObjectPool.h"
#include "ModuleBehavior.h"

Scene::~Scene()
{
    for (auto& gameObject : m_SceneObjects)
    {
        gameObject.reset();
    }
}

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    std::string uniqueName = GenerateUniqueGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);

	m_SceneObjects.push_back(sceneObject);

	const_cast<GameObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

	return sceneObject;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string_view& name, GameObjectType type, GameObject::Index parentIndex)
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

std::shared_ptr<GameObject> Scene::LoadGameObject(size_t instanceID, const std::string_view& name, GameObjectType type, GameObject::Index parentIndex)
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
    ScriptManager->SetReload(true);
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
	InternalPhysicsUpdateEvent.Broadcast(deltaSecond);
	// Internal Physics Update 작성
	PhysicsManagers->Update(deltaSecond);
	// OnTriggerEvent.Broadcast(); 작성
	CoroutineManagers->yield_WaitForFixedUpdate();
}

void Scene::OnTriggerEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnTriggerEnterEvent.TargetInvoke(
            target->m_onTriggerEnterEventHandle,collider);
    }
}

void Scene::OnTriggerStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnTriggerStayEvent.TargetInvoke(
            target->m_onTriggerStayEventHandle, collider);
    }
}

void Scene::OnTriggerExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnTriggerExitEvent.TargetInvoke(
            target->m_onTriggerExitEventHandle, collider);
    }
}

void Scene::OnCollisionEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnCollisionEnterEvent.TargetInvoke(
            target->m_onCollisionEnterEventHandle, collider);
    }
}

void Scene::OnCollisionStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnCollisionStayEvent.TargetInvoke(
            target->m_onCollisionStayEventHandle, collider);
    }
}

void Scene::OnCollisionExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponent<ModuleBehavior>();
    if (nullptr != target)
    {
        OnCollisionExitEvent.TargetInvoke(
            target->m_onCollisionExitEventHandle, collider);
    }
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
}

void Scene::OnDestroy()
{
    OnDestroyEvent.Broadcast();
    DistroyLight();
    DestroyGameObjects();
}

void Scene::AllDestroyMark()
{
    for (const auto& obj : m_SceneObjects)
    {
        if (obj && !obj->IsDestroyMark())
            obj->Destroy();
    }
}

uint32 Scene::UpdateLight(LightProperties& lightProperties) const
{
	memset(lightProperties.m_lights, 0, sizeof(Light) * MAX_LIGHTS);

	uint32 count{};
	for (int i = 0; i < m_lights.size(); ++i)
	{
		if (LightStatus::Disabled != m_lights[i].m_lightStatus)
		{
			lightProperties.m_lights[count++] = m_lights[i];
		}
	}

	return count;
}

std::pair<size_t, Light&> Scene::AddLight()
{
	Light& light = m_lights.emplace_back();
	light.m_lightStatus = LightStatus::Enabled;
	size_t index = m_lights.size() - 1;

	return std::pair<size_t, Light&>(index, light);
}

Light& Scene::GetLight(size_t index)
{
	if (index < m_lights.size())
	{
		return m_lights[index];
	}
	return m_lights[0];
}

void Scene::RemoveLight(size_t index)
{
	if (index < m_lights.size())
	{
		m_lights[index].m_lightType = LightType_InVaild;
		m_lights[index].m_lightStatus = LightStatus::Disabled;
		m_lights[index].m_intencity = 0.f;
		m_lights[index].m_color = { 0,0,0,0 };
	}
}

void Scene::DistroyLight()
{
    std::erase_if(m_lights, [](const Light& light) 
	{
		return light.m_lightType == LightType_InVaild;
	});
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
            for (auto& onDistroyComponent : obj->m_components)
            {
                auto behavior = std::dynamic_pointer_cast<ModuleBehavior>(onDistroyComponent);
                if (behavior)
                {
                    ScriptManager->UnCollectScriptComponent(obj.get(), obj->m_componentIds[behavior->GetTypeID()], behavior->m_name.ToString());
                }
            }

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
        {
            obj->m_parentIndex = indexMap[obj->m_parentIndex];
			obj->m_rootIndex = indexMap[obj->m_rootIndex];
        }
        else
        {
            obj->m_parentIndex = GameObject::INVALID_INDEX;
        }

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

std::string Scene::GenerateUniqueGameObjectName(const std::string_view& name)
{
	std::string uniqueName{ name.data() };
	std::string baseName{ name.data() };
	int count = 1;
	while (m_gameObjectNameSet.find(uniqueName) != m_gameObjectNameSet.end())
	{
		uniqueName = baseName + std::string(" (") + std::to_string(count++) + std::string(")");
	}
	m_gameObjectNameSet.insert(uniqueName);
	return uniqueName;
}

void Scene::RemoveGameObjectName(const std::string_view& name)
{
	m_gameObjectNameSet.erase(name.data());
}

