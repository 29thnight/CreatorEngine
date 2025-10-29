#include "GameObject.h"
#include "ModuleBehavior.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "TagManager.h"
#include "RectTransformComponent.h"

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
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
	m_componentIds.reserve(30); // Reserve space for component IDs to avoid frequent reallocations
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
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
	m_componentIds.reserve(30); // Reserve space for component IDs to avoid frequent reallocations

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
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
	m_componentIds.reserve(30); // Reserve space for component IDs to avoid frequent reallocations

	if (type == GameObjectType::UI || type == GameObjectType::Canvas)
	{
		AddComponent<RectTransformComponent>();
	}
}

const std::string& GameObject::RemoveSuffixNumberTag() const
{
	// 정규표현식: 끝에 오는 " (숫자)" 또는 "(숫자)" 패턴 제거
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

	m_destroyMark = true;

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

		if (auto receiver = std::dynamic_pointer_cast<IRegistableEvent>(component))
		{
			receiver->RegisterOverriddenEvents(this->GetScene());
		}

        m_components.push_back(component);

        m_componentIds[component->GetTypeID()] = m_components.size() - 1;
    }

	return component;
}

ModuleBehavior* GameObject::AddScriptComponent(std::string_view scriptName)
{
    std::shared_ptr<ModuleBehavior> component = std::shared_ptr<ModuleBehavior>(
		ScriptManager->CreateMonoBehavior(scriptName.data()), 
		[](ModuleBehavior* ptr) // Custom deleter to ensure proper cleanup
		{
			ScriptManager->DestroyMonoBehavior(ptr);
		}
	);
	if (!component)
	{
		Debug->LogError("Failed to create script component: " + std::string(scriptName));
		return nullptr;
	}
	component->SetOwner(this);

	std::string scriptFile = std::string(scriptName) + ".cpp";

	component->m_scriptGuid = DataSystems->GetFilenameToGuid(scriptFile);

    auto componentPtr = std::reinterpret_pointer_cast<Component>(component);
    m_components.push_back(componentPtr);
    
	size_t index = m_components.size() - 1;
	m_componentIds[component->m_scriptTypeID] = index;

    ScriptManager->CollectScriptComponent(this, index, scriptName.data());
	ScriptManager->BindScriptEvents(component.get(), scriptName.data());

    return component.get();
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

std::shared_ptr<Component> GameObject::GetComponentByTypeID(uint32 id)
{
	if (id >= m_components.size())
	{
		return nullptr;
	}
	auto iter = m_componentIds.find(id);
	if (iter != m_componentIds.end())
	{
		size_t index = iter->second;
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

		auto scriptComponent = std::dynamic_pointer_cast<ModuleBehavior>(m_components[i]);
		if (scriptComponent)
		{
			ScriptManager->UnCollectScriptComponent(this, i, scriptComponent->m_name.ToString());
			newMap[scriptComponent->m_scriptTypeID] = i;
			ScriptManager->CollectScriptComponent(this, i, scriptComponent->m_name.ToString());
		}
		else
		{
			newMap[component->GetTypeID()] = i;
		}
	}

	m_componentIds = std::move(newMap);
}

void GameObject::AddChild(GameObject* _objcet)
{
	auto scene = SceneManagers->GetActiveScene();
	auto oldParent = scene->GetGameObject(_objcet->m_parentIndex);

	auto& siblings = oldParent->m_childrenIndices;
	std::erase_if(siblings, [&](auto index) { return index == _objcet->m_index; });

	_objcet->m_parentIndex = m_index;
	m_childrenIndices.push_back(_objcet->m_index);
	_objcet->m_transform.SetParentID(m_index);
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
		m_components[index]->Destroy();
		m_componentIds.erase(iter);
	}
}

void GameObject::RemoveScriptComponent(std::string_view scriptName)
{
	auto iter = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>(); });
	if (iter != m_components.end())
	{
		auto scriptComponent = std::dynamic_pointer_cast<ModuleBehavior>(*iter);
		if (scriptComponent && scriptComponent->m_name == scriptName)
		{
			ScriptManager->UnbindScriptEvents(scriptComponent.get(), scriptName);
			ScriptManager->UnCollectScriptComponent(this, m_componentIds[scriptComponent->m_scriptTypeID], scriptComponent->m_name.ToString());
			scriptComponent->Destroy();
			RemoveComponentTypeID(scriptComponent->m_scriptTypeID);
		}
	}
}

void GameObject::RemoveScriptComponent(ModuleBehavior* ptr)
{
	auto iter = m_componentIds.find(ptr->m_scriptTypeID);
	if (iter != m_componentIds.end())
	{
		auto scriptComponent = ptr;
		auto scriptName = scriptComponent->m_name.ToString();
		if (scriptComponent && iter != m_componentIds.end())
		{
			size_t index = iter->second;
			ScriptManager->UnbindScriptEvents(scriptComponent, scriptName);
			ScriptManager->UnCollectScriptComponent(this, index, scriptName);
			scriptComponent->Destroy();
			RemoveComponentTypeID(scriptComponent->m_scriptTypeID);
		}
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
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=] (std::shared_ptr<GameObject>& object)
		{
			return object->m_instanceID == guid;
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
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
		{
			return object->m_attachedSoketID == guid;
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

GameObject* GameObject::OwnerSceneFindInstanceID(const HashedGuid& guid)
{
	Scene* scene = m_ownerScene;
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
			{
				return object->m_instanceID == guid;
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
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
			{
				return object->m_attachedSoketID == guid;
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
		if(m_ownerScene)
		{
			auto& childObj = m_ownerScene->m_SceneObjects[childObjIndex];
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


