#include "GameObject.h"
#pragma once

template<typename T>
inline T* GameObject::AddComponent()
{
    if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    T* component = new T();

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size();

    return component;
}

template<typename T>
inline T* GameObject::AddScriptComponent(const std::string_view& scriptName)
{
    if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    T* component = ScriptManager->CreateMonoBehavior(scriptName.data());
    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size();

    size_t index = m_componentIds[component->GetTypeID()];

    ScriptManager->CollectScriptComponent(this, index, scriptName.data());

    return component;
}

template<typename T, typename ...Args>
inline T* GameObject::AddComponent(Args && ...args)
{
    if (std::ranges::find_if(m_components, [&](Component* component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    T* component = new T(std::forward<Args>(args)...);
    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size();

    return component;
}

template<typename T>
inline T* GameObject::GetComponent(uint32 id)
{
    auto it = m_componentIds.find(id);
    if (it == m_componentIds.end())
        return nullptr;
    return static_cast<T*>(&m_components[it->second]);
}

template<typename T>
inline T* GameObject::GetComponent()
{
    for (auto& component : m_components)
    {
        if (T* castedComponent = dynamic_cast<T*>(component))
            return castedComponent;
    }
    return nullptr;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponents()
{
    std::vector<T*> comps;
    for (auto& component : m_components)
    {
        if (T* castedComponent = dynamic_cast<T*>(component))
            comps.push_back(castedComponent);
    }
    return comps;
}

template<typename T>
inline void GameObject::RemoveComponent(T* component)
{
    component->SetDestroyMark();
}
