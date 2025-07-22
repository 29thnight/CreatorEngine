#include "Scene.h"
#include "HotLoadSystem.h"
#include "GameObjectPool.h"
#include "ModuleBehavior.h"
#include "LightComponent.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "Animator.h"
#include "Skeleton.h"
#include "PhysicsManager.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "MeshCollider.h"
#include "CharacterControllerComponent.h"
#include "TerrainCollider.h"
#include "RigidBodyComponent.h"
#include "TagManager.h"

Scene::Scene()
{
    resetObjHandle = SceneManagers->resetSelectedObjectEvent.AddRaw(this, &Scene::ResetSelectedSceneObject);
}

Scene::~Scene()
{
    SceneManagers->resetSelectedObjectEvent -= resetObjHandle;
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

void Scene::AddRootGameObject(const std::string_view& name)
{
	std::string uniqueName{};

	if (name.empty())
	{
		uniqueName = GenerateUniqueGameObjectName("SampleScene");
	}
	else
	{
		uniqueName = GenerateUniqueGameObjectName(name);
	}

	GameObject::Index index = m_SceneObjects.size();
	auto ptr = ObjectPool::Allocate<GameObject>(uniqueName, GameObjectType::Empty, index, -1);
	if (nullptr == ptr)
	{
		return;
	}

	std::shared_ptr<GameObject> newObj(ptr, [&](GameObject* obj)
	{
		if (obj)
		{
			ObjectPool::Deallocate(obj);
		}
	});

	m_SceneObjects.push_back(newObj);
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string_view& name, GameObjectType type, GameObject::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }
    
    if (parentIndex >= m_SceneObjects.size())
    {
        parentIndex = -1; //&&&&&
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

	if (!newObj->m_tag.ToString().empty())
	{
		TagManager::GetInstance()->AddTagToObject(newObj->m_tag.ToString(), newObj.get());
	}

	if (!newObj->m_layer.ToString().empty())
	{
		TagManager::GetInstance()->AddObjectToLayer(newObj->m_layer.ToString(), newObj.get());
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

std::vector<std::shared_ptr<GameObject>> Scene::CreateGameObjects(size_t createSize, GameObject::Index parentIndex)
{
	std::vector<std::shared_ptr<GameObject>> createObjects{ createSize };
	for (int i = 0; i < createSize; ++i)
	{
		auto newObject = CreateGameObject("default", GameObjectType::Empty, parentIndex);
		if (newObject)
		{
			createObjects.push_back(newObject);
		}
	}

	return createObjects;
}

void Scene::Reset()
{
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
	SetInternalPhysicData();

    FixedUpdateEvent.Broadcast(deltaSecond);
	InternalPhysicsUpdateEvent.Broadcast(deltaSecond);
	// Internal Physics Update 작성
	PhysicsManagers->Update(deltaSecond);
	// OnTriggerEvent.Broadcast(); 작성
	CoroutineManagers->yield_WaitForFixedUpdate();
}

void Scene::OnTriggerEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
		OnTriggerEnterEvent.TargetInvoke(
			t->m_onTriggerEnterEventHandle, collider);
	}
}

void Scene::OnTriggerStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnTriggerStayEvent.TargetInvoke(
            t->m_onTriggerStayEventHandle, collider);
    }
}

void Scene::OnTriggerExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnTriggerExitEvent.TargetInvoke(
            t->m_onTriggerExitEventHandle, collider);
    }
}

void Scene::OnCollisionEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionEnterEvent.TargetInvoke(
            t->m_onCollisionEnterEventHandle, collider);
    }
}

void Scene::OnCollisionStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionStayEvent.TargetInvoke(
            t->m_onCollisionStayEventHandle, collider);
    }
}

void Scene::OnCollisionExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionExitEvent.TargetInvoke(
            t->m_onCollisionExitEventHandle, collider);
    }
}

void Scene::Update(float deltaSecond)
{
	AllUpdateWorldMatrix();

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
    DestroyLight();
    DestroyComponents();
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

void Scene::ResetSelectedSceneObject()
{
    m_selectedSceneObject = nullptr;
}

void Scene::CollectLightComponent(LightComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_lightComponents.begin(),
			m_lightComponents.end(), 
			[ptr](const auto& light)
			{
				return light == ptr;
			});

		if (it == m_lightComponents.end())
		{
			m_lightComponents.push_back(ptr);
		}
	}
}

void Scene::UnCollectLightComponent(LightComponent* ptr)
{
    if (ptr)
    {
		std::erase_if(m_lightComponents, [ptr](const auto& light) { return light == ptr; });
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
	if(index > m_lights.size() || 0 == m_lights.size())
	{
		m_lights.resize(index + 1);
	}

	return m_lights[index];
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

void Scene::DestroyLight()
{
    std::unordered_map<size_t, size_t> indexRemap;
    std::vector<Light> newLights;
    bool isFirstDirectional = false;

	newLights.reserve(m_lights.size());

    for (size_t i = 0; i < m_lights.size(); ++i)
    {
        if (m_lights[i].m_lightType != LightType_InVaild)
        {
            indexRemap[i] = newLights.size();
            newLights.push_back(m_lights[i]);
        }
    }

    m_lights = std::move(newLights);

	for (auto& comp : m_lightComponents)
	{
		if (!comp) continue;

		int&  lightIndex = comp->m_lightIndex;
		auto& lightType  = comp->m_lightType;
        if (auto it = indexRemap.find(lightIndex); it != indexRemap.end())
        {
            lightIndex = static_cast<int>(it->second);
        }

        if (!isFirstDirectional && lightType == LightType::DirectionalLight)
        {
            isFirstDirectional  = true;
            comp->m_lightStatus = LightStatus::StaticShadows;
        }
	}
}

void Scene::CollectMeshRenderer(MeshRenderer* ptr)
{
	if (ptr)
	{
		m_allMeshRenderers.push_back(ptr);
		if (ptr->IsSkinnedMesh())
		{
			m_skinnedMeshRenderers.push_back(ptr);
		}
		else
		{
			m_staticMeshRenderers.push_back(ptr);
		}
	}
}

void Scene::UnCollectMeshRenderer(MeshRenderer* ptr)
{
	if (ptr)
	{
        if (ptr->IsSkinnedMesh())
        {
			std::erase_if(m_skinnedMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
		}
		else
		{
			std::erase_if(m_staticMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
        }
		std::erase_if(m_allMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
	}
}

void Scene::CollectTerrainComponent(TerrainComponent* ptr)
{
	if (ptr)
	{
		m_terrainComponents.push_back(ptr);
	}
}

void Scene::UnCollectTerrainComponent(TerrainComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_terrainComponents, [ptr](const auto& mesh) { return mesh == ptr; });
	}
}

void Scene::CollectRigidBodyComponent(RigidBodyComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_rigidBodyComponents.begin(),
			m_rigidBodyComponents.end(),
			[ptr](const auto& body) { return body == ptr; });
		if (it == m_rigidBodyComponents.end())
		{
			m_rigidBodyComponents.push_back(ptr);
		}
	}
}

void Scene::UnCollectRigidBodyComponent(RigidBodyComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_rigidBodyComponents, [ptr](const auto& body) { return body == ptr; });
	}
}

void Scene::CollectColliderComponent(BoxColliderComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_boxColliderComponents.begin(),
			m_boxColliderComponents.end(),
			[ptr](const auto& box) { return box == ptr; });
		if (it == m_boxColliderComponents.end())
		{
			m_boxColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto boxInfo = ptr->GetBoxInfo();
			auto colliderID = boxInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(boxInfo, ptr->GetColliderType()); //&&&&&trigger
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{ m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
			else
			{
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(boxInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{
						m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
		};

		m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
	}
}

void Scene::UnCollectColliderComponent(BoxColliderComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_boxColliderComponents, [ptr](const auto& box) { return box == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
}

void Scene::CollectColliderComponent(SphereColliderComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_sphereColliderComponents.begin(),
			m_sphereColliderComponents.end(),
			[ptr](const auto& sphere) { return sphere == ptr; });
		if (it == m_sphereColliderComponents.end())
		{
			m_sphereColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto sphereInfo = ptr->GetSphereInfo();
			auto colliderID = sphereInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(sphereInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{ m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
			else
			{
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(sphereInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{
						m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
		};

		m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
	}
}

void Scene::UnCollectColliderComponent(SphereColliderComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_sphereColliderComponents, [ptr](const auto& sphere) { return sphere == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
}

void Scene::CollectColliderComponent(CapsuleColliderComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_capsuleColliderComponents.begin(),
			m_capsuleColliderComponents.end(),
			[ptr](const auto& capsule) { return capsule == ptr; });
		if (it == m_capsuleColliderComponents.end())
		{
			m_capsuleColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto capsuleInfo = ptr->GetCapsuleInfo();
			auto colliderID = capsuleInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(capsuleInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{ m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
			else
			{
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(capsuleInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{
						m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
		};

		m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
	}
}

void Scene::UnCollectColliderComponent(CapsuleColliderComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_capsuleColliderComponents, [ptr](const auto& capsule) { return capsule == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
}

void Scene::CollectColliderComponent(MeshColliderComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_meshColliderComponents.begin(),
			m_meshColliderComponents.end(),
			[ptr](const auto& mesh) { return mesh == ptr; });
		if (it == m_meshColliderComponents.end())
		{
			m_meshColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto convexMeshInfo = ptr->GetMeshInfo();
			auto colliderID = convexMeshInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(convexMeshInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{ m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
			else
			{
				bool isKinematic = bodyType == EBodyType::KINEMATIC;
				Physics->CreateDynamicBody(convexMeshInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
				m_colliderContainer[colliderID] =
					PhysicsManager::ColliderInfo{
						m_boxTypeId,
						ptr,
						ptr->GetOwner(),
						ptr,
						false
				};
			}
		};

		m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
	}
}

void Scene::UnCollectColliderComponent(MeshColliderComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_meshColliderComponents, [ptr](const auto& mesh) { return mesh == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
}

void Scene::CollectColliderComponent(CharacterControllerComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_characterControllerComponents.begin(),
			m_characterControllerComponents.end(),
			[ptr](const auto& character) { return character == ptr; });
		if (it == m_characterControllerComponents.end())
		{
			m_characterControllerComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto controllerInfo = ptr->GetControllerInfo();
		auto colliderID = controllerInfo.id;

		m_colliderContainer[colliderID] =
			PhysicsManager::ColliderInfo{ m_controllerTypeId,
				ptr,
				ptr->GetOwner(),
				ptr,
				false
		};
	}
}

void Scene::CollectColliderComponent(TerrainColliderComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_terrainColliderComponents.begin(),
			m_terrainColliderComponents.end(),
			[ptr](const auto& terrain) { return terrain == ptr; });
		if (it == m_terrainColliderComponents.end())
		{
			m_terrainColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto gameObject = ptr->GetOwner();
		auto heightFieldInfo = ptr->GetHeightFieldColliderInfo();
		auto colliderID = heightFieldInfo.colliderInfo.id;

		m_colliderContainer.insert({ colliderID, {
			m_heightFieldTypeId,
			ptr, // corrected variable name from 'collider' to 'ptr'
			gameObject,
			ptr,
			false
		} });
	}
}

void Scene::UnCollectColliderComponent(CharacterControllerComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_characterControllerComponents, [ptr](const auto& character) { return character == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
}

void Scene::UnCollectColliderComponent(TerrainColliderComponent* ptr)
{
	if (ptr)
	{
		std::erase_if(m_terrainColliderComponents, [ptr](const auto& terrain) { return terrain == ptr; });

		PhysicsManagers->RemoveCollider(ptr);
	}
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

    for (auto& obj : m_SceneObjects)
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
			obj->m_transform.SetParentID(obj->m_parentIndex);
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

void Scene::DestroyComponents()
{
	for (auto& obj : m_SceneObjects)
	{
		if (obj)
		{
			for (auto& component : obj->m_components)
			{
				if (!component || !component->IsDestroyMark() || component->IsDontDestroyOnLoad())
				{
					continue;
				}

				auto behavior = std::dynamic_pointer_cast<ModuleBehavior>(component);
				if (behavior)
				{
					obj->RemoveScriptComponent(behavior.get());
				}
				else
				{
					obj->RemoveComponentTypeID(component->GetTypeID());
				}

				component.reset();
			}

			std::erase_if(obj->m_components, [](const auto& component)
			{
				auto behavior = std::dynamic_pointer_cast<ModuleBehavior>(component);
				return component == nullptr;
			});

			obj->RefreshComponentIdIndices();
		}
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

void Scene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model)
{
	const auto& obj = GetGameObject(objIndex);

	if (!obj || obj->IsDestroyMark())
	{
		return;
	}

	if (GameObjectType::Bone == obj->GetType())
	{
		const auto& animator = GetGameObject(obj->m_rootIndex)->GetComponent<Animator>();
		if (!animator || !animator->m_Skeleton || !animator->IsEnabled())
		{
			return;
		}
		const auto& bone = animator->m_Skeleton->FindBone(obj->m_name.ToString());
		if (bone)
		{
			obj->m_transform.SetAndDecomposeMatrix(bone->m_globalTransform);
		}
	}
	else
	{
		if (obj->m_transform.IsDirty())
		{
			auto renderer = obj->GetComponent<MeshRenderer>();
			if (renderer)
			{
				renderer->SetNeedUpdateCulling(true);
			}
		}
		model = XMMatrixMultiply(obj->m_transform.GetLocalMatrix(), model);
		obj->m_transform.SetAndDecomposeMatrix(model);

	}

	for (auto& childIndex : obj->m_childrenIndices)
	{
		UpdateModelRecursive(childIndex, model);
	}
}

void Scene::SetInternalPhysicData()
{
	std::unordered_map<GameObject*, EBodyType> m_bodyType;

	for (auto& rigid : m_rigidBodyComponents)
	{
		auto gameObject = rigid->GetOwner();
		m_bodyType[gameObject] = rigid->GetBodyType();
	}

	std::unordered_set<GameObject*> linkCompleteSet;
	for (auto& box : m_boxColliderComponents)
	{
		if (box && box->GetOwner())
		{
			auto gameObject = box->GetOwner();
			auto iter = m_ColliderTypeLinkCallback.find(gameObject);
			if(iter != m_ColliderTypeLinkCallback.end())
			{
				
				iter->second(m_bodyType[gameObject]);
			}
			linkCompleteSet.insert(gameObject);
		}
	}

	for (auto& sphere : m_sphereColliderComponents)
	{
		if (sphere && sphere->GetOwner())
		{
			auto gameObject = sphere->GetOwner();
			auto iter = m_ColliderTypeLinkCallback.find(gameObject);
			if(iter != m_ColliderTypeLinkCallback.end())
			{
				iter->second(m_bodyType[gameObject]);
			}
			linkCompleteSet.insert(gameObject);
		}
	}

	for (auto& capsule : m_capsuleColliderComponents)
	{
		if (capsule && capsule->GetOwner())
		{
			auto gameObject = capsule->GetOwner();
			auto iter = m_ColliderTypeLinkCallback.find(gameObject);
			if(iter != m_ColliderTypeLinkCallback.end())
			{
				iter->second(m_bodyType[gameObject]);
			}
			linkCompleteSet.insert(gameObject);
		}
	}

	for (auto& mesh : m_meshColliderComponents)
	{
		if (mesh && mesh->GetOwner())
		{
			auto gameObject = mesh->GetOwner();
			auto iter = m_ColliderTypeLinkCallback.find(gameObject);
			if(iter != m_ColliderTypeLinkCallback.end())
			{
				iter->second(m_bodyType[gameObject]);
			}
			linkCompleteSet.insert(gameObject);
		}
	}

	std::erase_if(m_ColliderTypeLinkCallback, 
		[&linkCompleteSet](const auto& pair)
		{
			return linkCompleteSet.contains(pair.first);
		});
}

void Scene::AllUpdateWorldMatrix()
{
	for (auto& objIndex : m_SceneObjects[0]->m_childrenIndices)
	{
		UpdateModelRecursive(objIndex, XMMatrixIdentity());
	}
}

