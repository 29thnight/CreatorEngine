#include "GameObject.h"
#include "ModuleBehavior.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"

GameObject::GameObject() :
	Object("GameObject"),
	m_gameObjectType(GameObjectType::Empty),
	m_index(0),
	m_parentIndex(0)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
}

GameObject::GameObject(const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
}

GameObject::GameObject(size_t instanceID, const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
	Object(name, instanceID),
	m_gameObjectType(type),
	m_index(index),
	m_parentIndex(parentIndex)
{
	m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
}

std::shared_ptr<Component> GameObject::AddComponent(const Meta::Type& type)
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == type.typeID; }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<Component> component = std::shared_ptr<Component>(Meta::MetaFactoryRegistry->Create<Component>(type.name));
	
    if (component)
    {
        m_components.push_back(component);
        component->SetOwner(this);
        m_componentIds[component->GetTypeID()] = m_components.size();
    }

	return component;
}

ModuleBehavior* GameObject::AddScriptComponent(const std::string_view& scriptName)
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>(); }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<ModuleBehavior> component = std::shared_ptr<ModuleBehavior>(ScriptManager->CreateMonoBehavior(scriptName.data()));
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
    m_componentIds[component->m_scriptTypeID] = m_components.size() - 1;

    size_t index = m_componentIds[component->m_scriptTypeID];

    ScriptManager->CollectScriptComponent(this, index, scriptName.data());

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


void GameObject::RemoveComponent(uint32 id)
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
	m_components[id]->Destroy();
}

void GameObject::RemoveComponent(const std::string_view& scriptName)
{
	auto iter = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>(); });
	if (iter != m_components.end())
	{
		auto scriptComponent = std::dynamic_pointer_cast<ModuleBehavior>(*iter);
		if (scriptComponent && scriptComponent->m_name == scriptName)
		{
			RemoveComponent(scriptComponent->GetTypeID());
			ScriptManager->UnbindScriptEvents(scriptComponent.get(), scriptName);
			ScriptManager->UnCollectScriptComponent(this, m_componentIds[scriptComponent->GetTypeID()], scriptComponent->m_name.ToString());
		}
	}
}

void GameObject::RemoveComponent(Meta::Type& type)
{
}

GameObject* GameObject::Find(const std::string_view& name)
{
    Scene* scene = SceneManagers->GetActiveScene();
    if (scene)
    {
        return scene->GetGameObject(name).get();
    }
    return nullptr;
}


