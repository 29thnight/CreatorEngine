#include "Prefab.h"

Prefab::Prefab(const std::string_view& name, const GameObject* source)
    : Object(name)
{
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<Prefab>();
    if (source)
    {
        m_prefabData = SerializeRecursive(source);
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

    GameObject::Index parent = m_prefabData["m_parentIndex"].as<GameObject::Index>();
    return InstantiateRecursive(m_prefabData, scene, parent, newName);
}

MetaYml::Node Prefab::SerializeRecursive(const GameObject* obj)
{
    MetaYml::Node node;
    if (!obj)
        return node;

    const Meta::Type* type = Meta::MetaDataRegistry->Find(obj->GetTypeID());
    if (type)
    {
        GameObject* nonConst = const_cast<GameObject*>(obj);
        node = Meta::Serialize(nonConst, *type);
    }

    if (!obj->m_childrenIndices.empty())
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (scene)
        {
            MetaYml::Node childrenNode;
            for (auto childIndex : obj->m_childrenIndices)
            {
                auto childObj = scene->GetGameObject(childIndex);
                if (childObj)
                    childrenNode.push_back(SerializeRecursive(childObj.get()));
            }
            if (childrenNode)
                node["children"] = childrenNode;
        }
    }

    return node;
}

GameObject* Prefab::InstantiateRecursive(const MetaYml::Node& node,
                                         Scene* scene,
                                         GameObject::Index parent,
                                         const std::string_view& overrideName) const
{
    if (!scene || !node)
        return nullptr;

    GameObjectType type = static_cast<GameObjectType>(node["m_gameObjectType"].as<int>());
    std::string objName = overrideName.empty() ? node["m_name"].as<std::string>() : std::string(overrideName);

    auto objPtr = scene->LoadGameObject(make_guid(), objName, type, parent);
    GameObject* obj = objPtr.get();
    if (!obj)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<GameObject>());
    GameObject::Index newIndex = obj->m_index;
    if (meta)
    {
        Meta::Deserialize(obj, node);
    }

    obj->m_index = newIndex;
    obj->m_parentIndex = parent;
    obj->m_transform.SetParentID(parent);

    if (node["m_components"])
    {
        for (const auto& componentNode : node["m_components"])
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

    obj->m_childrenIndices.clear();
    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            auto childObj = InstantiateRecursive(childNode, scene, obj->m_index);
            if (childObj)
                obj->m_childrenIndices.push_back(childObj->m_index);
        }
    }

    return obj;
}

