#pragma once
#include "GameObject.h"

template<typename T>
inline T* GameObject::AddComponent()
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>();
    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;

    return component.get();
}

template<typename T, typename ...Args>
inline T* GameObject::AddComponent(Args && ...args)
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>(std::forward<Args>(args)...);
    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;

    return component.get();
}

template<typename T>
inline T* GameObject::GetComponent(uint32 id)
{
    auto it = m_componentIds.find(id);
    if (it == m_componentIds.end())
        return nullptr;
    return static_cast<T*>(&m_components[it->second].get());
}

template<typename T>
inline T* GameObject::GetComponent()
{
    for (auto& component : m_components)
    {
        std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component);
        if (nullptr != castedComponent.get())
            return castedComponent.get();
    }
    return nullptr;
}

template<>
inline Transform* GameObject::GetComponent()
{
    return &m_transform;
}

template<typename T>
inline bool GameObject::HasComponent()
{
	for (auto& [typeID, index ] : m_componentIds)
	{
		if (typeID == TypeTrait::GUIDCreator::GetTypeID<T>())
		{
			return true;
		}
	}
	return false;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponents()
{
    std::vector<T*> comps;
    for (auto& component : m_components)
    {
        if (std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component))
            comps.push_back(castedComponent.get());
    }
    return comps;
}

template<typename T>
inline void GameObject::RemoveComponent(T* component)
{
    component->Destroy();
	auto it = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> comp) { return comp.get() == component; });

	if (it != m_components.end())
	{
		if (ModuleBehavior* script = dynamic_cast<ModuleBehavior*>(component))
		{
			RemoveScriptComponent(script);
		}
        else
        {
            m_componentIds.erase(component->GetTypeID());
        }
	}
}
