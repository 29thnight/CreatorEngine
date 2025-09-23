#pragma once
#include "GameObject.h"
#include "IRegistableEvent.h"

template<typename T>
inline T* GameObject::AddComponent()
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<T>(); }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<T> component = shared_alloc<T>();
    if (auto receiver = std::dynamic_pointer_cast<IRegistableEvent>(component))
    {
        receiver->RegisterOverriddenEvents(this->GetScene());
    }

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;

    if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
    {
        initializable->Initialize();
    }

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
    if (auto receiver = std::dynamic_pointer_cast<IRegistableEvent>(component))
    {
        receiver->RegisterOverriddenEvents(this->GetScene());
    }

    m_components.push_back(component);
    component->SetOwner(this);
    m_componentIds[component->GetTypeID()] = m_components.size() - 1;

    if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
    {
        initializable->Initialize();
    }

    return component.get();
}

template<typename T>
inline T* GameObject::GetComponent(uint32 index)
{
    return std::static_pointer_cast<T>(m_components[index]).get();
}

template<typename T>
inline T* GameObject::GetComponent()
{
    auto it = m_componentIds.find(type_guid(T));
    if (it == m_componentIds.end())
        return nullptr;
    return std::static_pointer_cast<T>(m_components[it->second]).get();
}

template<typename T>
inline T* GameObject::GetComponentDynamicCast()
{
    for (auto& component : m_components)
    {
        std::shared_ptr<T> castedComponent = std::dynamic_pointer_cast<T>(component);
        if (nullptr != castedComponent.get())
            return castedComponent.get();
    }
    return nullptr;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponentsInChildren()
{
    std::vector<T*> comps;
    if (m_ownerScene)
    {
        for (auto& childIndex : m_childrenIndices)
        {
            auto& childObj = m_ownerScene->m_SceneObjects[childIndex];
            if (childObj)
            {
                if (T* comp = childObj->GetComponent<T>())
                {
                    comps.push_back(comp);
                }
                auto childComps = childObj->GetComponentsInChildren<T>();
                comps.insert(comps.end(), childComps.begin(), childComps.end());
            }
        }
    }
	return comps;
}

template<typename T>
inline std::vector<T*> GameObject::GetComponentsInchildrenDynamicCast() {
    std::vector<T*> comps;
    if (m_ownerScene)
    {
        for (auto& childIndex : m_childrenIndices)
        {
            auto& childObj = m_ownerScene->m_SceneObjects[childIndex];
            if (childObj)
            {
                if (T* comp = childObj->GetComponentDynamicCast<T>())
                {
                    comps.push_back(comp);
                }
                auto childComps = childObj->GetComponentsInchildrenDynamicCast<T>();
                comps.insert(comps.end(), childComps.begin(), childComps.end());
            }
        }
    }
    return comps;
}

template<>
inline Transform* GameObject::GetComponent()
{
    return &m_transform;
}

template<typename T>
inline bool GameObject::HasComponent()
{
	m_componentIds.find(type_guid(T));
	return m_componentIds.find(type_guid(T)) != m_componentIds.end();

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
