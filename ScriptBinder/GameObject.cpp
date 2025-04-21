#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"

GameObject::GameObject() :
	Object("GameObject"),
	m_gameObjectType(Type::Empty),
	m_index(0),
	m_parentIndex(0)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
}

GameObject::GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
}

GameObject::GameObject(size_t instanceID, const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex) :
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

GameObject* GameObject::Find(const std::string_view& name)
{
    Scene* scene = SceneManagers->GetActiveScene();
    if (scene)
    {
        return scene->GetGameObject(name).get();
    }
    return nullptr;
}


