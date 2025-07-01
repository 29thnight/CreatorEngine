#include "Prefab.h"

Prefab::Prefab(const std::string_view& name, const GameObject* source)
    : Object(name)
{
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<Prefab>();
    if (source)
    {
        const Meta::Type* type = Meta::MetaDataRegistry->Find(source->GetTypeID());
        if (type)
        {
            GameObject* nonConst = const_cast<GameObject*>(source);
            m_prefabData = Meta::Serialize(nonConst, *type);
        }
    }
}

Prefab* Prefab::CreateFromGameObject(const GameObject* source, const std::string_view& name)
{
    if (!source)
        return nullptr;

    std::string prefabName = name.empty() ? source->GetHashedName().ToString() + "_Prefab" : std::string(name);
    return new Prefab(prefabName, source);
}

GameObject* Prefab::Instantiate(const std::string_view& newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene)
        return nullptr;

    GameObjectType type = (GameObjectType)m_prefabData["m_gameObjectType"].as<int>();
    GameObject::Index parent = m_prefabData["m_parentIndex"].as<GameObject::Index>();

    auto objPtr = scene->LoadGameObject(make_guid(), newName.empty() ? m_prefabData["m_name"].as<std::string>() : std::string(newName), type, parent);
    GameObject* obj = objPtr.get();

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<GameObject>());
    if (meta && obj)
    {
        Meta::Deserialize(obj, m_prefabData);
    }

    if (m_prefabData["m_components"])
    {
        for (const auto& componentNode : m_prefabData["m_components"])
        {
            try
            {
                ComponentFactorys->LoadComponent(obj, componentNode);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(e.what());
                continue;
            }
        }
    }

    return obj;
}

