#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "TagManager.h"
#include "RectTransformComponent.h"
#include "PrefabUtility.h"
#ifndef DYNAMICCPP_EXPORTS
#include "ScriptObjectRegistry.h"
#endif

GameObject::GameObject() :
	Object("GameObject"),
	m_gameObjectType(GameObjectType::Empty),
	m_index(0),
	m_parentIndex(-1)
{
	m_ownerScene = SceneManagers->GetActiveScene();
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(0);
}

GameObject::GameObject(Scene* scene, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(parentIndex);

	if (type == GameObjectType::UI || type == GameObjectType::Canvas)
	{
		AddComponent<RectTransformComponent>();
	}
}

GameObject::GameObject(Scene* scene, size_t instanceID, std::string_view name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
	Object(name, instanceID),
	m_gameObjectType(type),
	m_index(index),
	m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
	m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(parentIndex);

	if (type == GameObjectType::UI || type == GameObjectType::Canvas)
	{
		AddComponent<RectTransformComponent>();
	}
}

const std::string& GameObject::RemoveSuffixNumberTag() const
{
	// ����ǥ����: ���� ���� " (����)" �Ǵ� "(����)" ���� ����
	return m_removedSuffixNumberTag;
}

void GameObject::SetTag(std::string_view tag)
{
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

    if (TagManager::GetInstance()->HasTag(tag))
    {
            m_tag = tag.data();
    }
}

void GameObject::SetLayer(std::string_view layer)
{
    if (layer.empty())
    {
        return;
    }

    if (TagManager::GetInstance()->HasLayer(layer))
    {
		size_t layerIndex = TagManager::GetInstance()->GetLayerIndex(layer); // Ensure layer is registered
        m_layer = layer.data();
		m_collisionType = (uint32)layerIndex;
    }
}

void GameObject::Destroy()
{
	if (m_destroyMark)
	{
		return;
	}

	TagManager::GetInstance()->RemoveTagFromObject(m_tag.ToString(), this);
	TagManager::GetInstance()->RemoveObjectFromLayer(m_layer.ToString(), this);

	// 프리팹 인스턴스 목록에서 뺀다. 넣기만 하고 빼는 곳이 없어서 죽은 포인터가
	// 목록에 남았고, 다음 UpdateInstances가 그것을 역참조했다.
	PrefabUtilitys->UnregisterInstance(this);

	m_destroyMark = true;
	TypeTrait::GUIDCreator::EraseGUID(m_instanceID);
#ifndef DYNAMICCPP_EXPORTS
	ScriptObjectRegistry::Get().Unregister(this);
#endif

	for (auto& component : m_components)
	{
		if (!component) continue;

		component->Destroy();
	}

	for (auto& childIndex : m_childrenIndices)
	{
		GameObject* child = FindIndex(childIndex);
		if (child)
		{
			child->Destroy();
		}
	}
}

void GameObject::AttachComponentLifecycle(const std::shared_ptr<Component>& component)
{
    if (!component) return;

    Scene* scene = this->GetScene();
    if (nullptr == scene) return;

    // 소유자는 아직 안 붙었을 수 있지만(호출부마다 순서가 다르다) 등록은 typeID만
    // 보므로 무관하다 — 소유자는 Awake를 부를 때 확인한다.
    scene->RegisterComponent(component.get());
}

std::shared_ptr<Component> GameObject::AddComponent(const Meta::Type& type)
{
    if (auto it = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == type.typeID; }); it != m_components.end())
    {
		Debug->LogWarning("Component of type " + type.name + " already exists on GameObject " + m_name.ToString() + ". Only one instance allowed.");
		return *it;
    }

    std::shared_ptr<Component> component = std::shared_ptr<Component>(Meta::MetaFactoryRegistry->CreateShared<Component>(type.name));
	
    if (component)
    {
		component->SetOwner(this);

		AttachComponentLifecycle(component);

        m_components.push_back(component);

        m_componentIds[component->GetTypeID()] = m_components.size() - 1;

		// K1-a 후속 배선: Meta::Type 경유 부착(디스크 로드 경로, ComponentFactory::
		// LoadComponent가 실제로 부른다)도 템플릿 AddComponent<T>()와 동일하게
		// 마스크 비트를 세워야 HasComponent<T>()가 로드된 오브젝트에서도 맞는다.
		const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(component->GetTypeID());
		if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid)
		{
			m_componentTypeMask |= (1ull << maskIndex);
		}
    }

	return component;
}

std::shared_ptr<Component> GameObject::AddComponentAllowMultiple(const Meta::Type& type)
{
	std::shared_ptr<Component> component = std::shared_ptr<Component>(
		Meta::MetaFactoryRegistry->CreateShared<Component>(type.name));

	if (!component)
	{
		return nullptr;
	}

	component->SetOwner(this);

	AttachComponentLifecycle(component);

	m_components.push_back(component);

	// 타입→인덱스 맵은 하나만 담을 수 있으므로 처음 것만 등록한다.
	// 여러 개를 다룰 때는 GetComponents로 순회해야 한다.
	if (m_componentIds.find(component->GetTypeID()) == m_componentIds.end())
	{
		m_componentIds[component->GetTypeID()] = m_components.size() - 1;
	}

	// K1-a 후속 배선: 다중 부착 경로(ScriptComponent 등)도 마스크를 세운다.
	// 이미 켜져 있으면(같은 타입 두 번째 이상 부착) 아무 효과 없는 OR라 안전하다.
	const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(component->GetTypeID());
	if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid)
	{
		m_componentTypeMask |= (1ull << maskIndex);
	}

	return component;
}

std::shared_ptr<Component> GameObject::GetComponent(const Meta::Type& type)
{
    HashedGuid typeID = type.typeID;
    auto iter = m_componentIds.find(typeID);
    if (iter != m_componentIds.end())
    {
        size_t index = m_componentIds[typeID];
        return m_components[index];
    }

    return nullptr;
}

void GameObject::RefreshComponentIdIndices()
{
	std::unordered_map<HashedGuid, size_t> newMap;

	for (size_t i = 0; i < m_components.size(); ++i)
	{
		const auto& component = m_components[i];
		if (!component)
			continue;

		newMap[component->GetTypeID()] = i;
	}

	m_componentIds = std::move(newMap);

	// K1-a 후속 배선: 이 함수는 컴포넌트 벡터가 통째로 재배치된 뒤 불린다
	// (Scene::DestroyComponents가 호출) — 인덱스맵과 마찬가지로 마스크도
	// 처음부터 다시 세워야 남은 타입 집합과 어긋나지 않는다.
	RebuildComponentTypeMask();
}

void GameObject::AddChild(GameObject* _objcet)
{
	auto scene = SceneManagers->GetActiveScene();
	if (!scene || !_objcet) return;

	// _objcet의 이전 부모가 없으면(최상위 오브젝트) 씬 루트의 children에서 자신을
	// 뗀다 — CreateGameObject 등 "부모 미지정 = 루트"인 관례를 여기서도 명시한다.
	// Scene::GetGameObject의 루트 폴백이 예전엔 이 자리에서도 암묵적으로 같은
	// 일을 했다(N-13) — 폴백이 사라진 지금은 무효 핸들을 nullptr로 돌려주므로
	// 직접 채워야 한다.
	auto oldParent = scene->GetGameObject(_objcet->m_parentIndex);
	if (!oldParent)
	{
		oldParent = scene->GetGameObject(0);
	}

	if (oldParent)
	{
		auto& siblings = oldParent->m_childrenIndices;
		std::erase_if(siblings, [&](auto index) { return index == _objcet->m_index; });
	}

	_objcet->SetParentIndex(m_index);
	m_childrenIndices.push_back(_objcet->m_index);
}

void GameObject::SetParentIndex(GameObject::Index parentIndex)
{
	m_parentIndex = parentIndex;
	m_transform.SetParentID(parentIndex);
}

void GameObject::RemoveComponentIndex(uint32 id)
{
	if (id >= m_components.size())
	{
		return;
	}

	auto iter = m_componentIds.find(m_components[id]->GetTypeID());
	if (iter != m_componentIds.end())
	{
		m_componentIds.erase(iter);
	}

	if(nullptr != m_components[id])
	{
		m_components[id]->Destroy();
	}
}

void GameObject::RemoveComponentTypeID(uint32 typeID)
{
	auto iter = m_componentIds.find(typeID);
	if (iter != m_componentIds.end())
	{
		size_t index = iter->second;
		if (m_components[index])
		{
			m_components[index]->Destroy();
		}
		m_componentIds.erase(iter);
	}
}

void GameObject::RemoveComponent(Meta::Type& type)
{
}

GameObject* GameObject::Find(std::string_view name)
{
    Scene* scene = SceneManagers->GetActiveScene();
    if (scene)
    {
        return scene->GetGameObject(name).get();
    }
    return nullptr;
}

GameObject* GameObject::FindIndex(GameObject::Index index)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		return scene->GetGameObject(index).get();
	}
	return nullptr;
}

GameObject* GameObject::FindInstanceID(const HashedGuid& guid)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1) — free 리스트로 회수된
		// 슬롯이 재사용되기 전까지 m_SceneObjects에 계속 남는다.
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=] (std::shared_ptr<GameObject>& object)
		{
			return object && object->m_instanceID == guid;
		});

		if (it != gameObjects.end())
		{
			return (*it).get();
		}
	}

	return nullptr;
}

GameObject* GameObject::FindAttachedID(const HashedGuid& guid)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1).
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
		{
			return object && object->m_attachedSoketID == guid;
		});
		if (it != gameObjects.end())
			return (*it).get();
	}

	return nullptr;
}

GameObject* GameObject::OwnerSceneFind(std::string_view name)
{
	Scene* scene = m_ownerScene;
	if (scene)
	{
		return scene->GetGameObject(name).get();
	}
	return nullptr;
}

GameObject* GameObject::OwnerSceneFindIndex(GameObject::Index index)
{
	Scene* scene = m_ownerScene;
	if (scene)
	{
		return scene->GetGameObject(index).get();
	}
	return nullptr;
}

GameObject* GameObject::SceneObjectAt(GameObject::Index index) const
{
	// GameObject.inl의 자식 순회 전용 우회 — 기존 inl 코드와 동일하게
	// m_SceneObjects를 직접 인덱싱한다(범위 검사 없음도 기존과 동일).
	if (!m_ownerScene) return nullptr;
	return m_ownerScene->m_SceneObjects[index].get();
}

GameObject* GameObject::OwnerSceneFindInstanceID(const HashedGuid& guid)
{
	Scene* scene = m_ownerScene;
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1).
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
			{
				return object && object->m_instanceID == guid;
			});

		if (it != gameObjects.end())
		{
			return (*it).get();
		}
	}

	return nullptr;
}

GameObject* GameObject::OwnerSceneFindAttachedID(const HashedGuid& guid)
{
	Scene* scene = m_ownerScene;
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1).
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
			{
				return object && object->m_attachedSoketID == guid;
			});
		if (it != gameObjects.end())
			return (*it).get();
	}

	return nullptr;
}

void GameObject::SetEnabled(bool able)
{
	if (m_isEnabled == able)
	{
		return;
	}
	m_isEnabled = able;

	for (auto& component : m_components)
	{
		if (component)
		{
			component->SetEnabled(able);
		}
	}

	for (auto& childObjIndex : m_childrenIndices)
	{
		if (!m_ownerScene) continue;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1) — children 목록은
		// 파괴 단일점이 정리해 주지만, 방어적으로 한 번 더 확인한다.
		if (!GameObject::IsValidIndex(childObjIndex) ||
			static_cast<size_t>(childObjIndex) >= m_ownerScene->m_SceneObjects.size())
		{
			continue;
		}

		auto& childObj = m_ownerScene->m_SceneObjects[childObjIndex];
		if (childObj)
		{
			childObj->SetEnabled(able);
		}
	}
}

void GameObject::SetCollisionType()
{
	size_t index = TagManager::GetInstance()->GetLayerIndex(m_layer.ToString());
	if (index >= TagManager::GetInstance()->GetLayers().size() || index > 32)
	{
		Debug->LogError("Invalid layer index: " + std::to_string(index));
		return;
	}

	m_collisionType = (uint32)index; // Set the bit corresponding to the layer index
}


