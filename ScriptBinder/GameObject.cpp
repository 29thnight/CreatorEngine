#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"

GameObject::GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex)
{
}

void GameObject::ComponentsSort()
{
    std::ranges::sort(m_components, [&](Component* a, Component* b)
    {
        return a->GetOrderID() < b->GetOrderID();
    });

    std::ranges::for_each(std::views::iota(0, static_cast<int>(m_components.size())), [&](int i)
    {
        m_componentIds[m_components[i]->GetTypeID()] = i;
    });
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
