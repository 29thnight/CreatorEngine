#include "Prefab.h"
#include "GameObject.h"
#include "PrefabUtility.h"
#include "TagManager.h"

Prefab::Prefab(std::string_view name, const GameObject* source)
    : Object(name)
{
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<Prefab>();
    if (source)
    {
        m_prefabData = SerializeRecursive(source);
    }
}

Prefab* Prefab::CreateFromGameObject(const GameObject* source, std::string_view name)
{
    if (!source)
        return nullptr;

    std::string prefabName = name.empty() ? source->GetHashedName().ToString() + "_Prefab" : std::string(name);
    return new Prefab(prefabName, source);
}

GameObject* Prefab::Instantiate(std::string_view newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene)
        return nullptr;

    if (!m_prefabData || !m_prefabData.IsSequence() || m_prefabData.size() == 0)
        return nullptr;
	Benchmark bm;
    auto gameObjNode = m_prefabData["GameObject"];

    GameObject* rootObject = nullptr;

    for (std::size_t i = 0; i < m_prefabData.size(); ++i)
    {
        const MetaYml::Node& gameObjNode = m_prefabData[i];

        // 첫 번째 GameObject에만 overrideName 적용
        std::string_view nameOverride = (i == 0) ? newName : "";

        GameObject* instantiated = InstantiateRecursive(gameObjNode, scene, 0, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }

	std::cout << "Prefab instantiated in " << bm.GetElapsedTime() << " ms\n";

    return rootObject;
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
    std::string_view overrideName) const
{
    if (!scene || !node)
        return nullptr;

    GameObjectType type = static_cast<GameObjectType>(node["m_gameObjectType"].as<int>());
    std::string objName = overrideName.empty() ? node["m_name"].as<std::string>() : std::string(overrideName);
    Benchmark bm;
    auto objPtr = scene->LoadGameObject(make_guid(), objName, type, parent);
    GameObject* obj = objPtr.get();
    if (!obj)
        return nullptr;

	std::cout << "GameObject Create Time: " << bm.GetElapsedTime() << " ms\n";

    Benchmark bm3;
    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<GameObject>());
    HashedGuid newInstanceID = obj->GetInstanceID();
	HashingString newHashedName = obj->GetHashedName();
    GameObject::Index newIndex = obj->m_index;
    if (meta)
    {
        try
        {
            Meta::Deserialize(obj, node);
        }
        catch (const std::exception& e)
        {
            Debug->LogError("Prefab instantiation failed: " + std::string(e.what()));
            return nullptr;
		}
    }
    std::cout << "Deserialize Time: " << bm3.GetElapsedTime() << " ms\n";

    if (type != GameObjectType::UI)
    {
        obj->m_instanceID = newInstanceID;
    }
	obj->m_name = newHashedName;
    obj->m_index = newIndex;
    obj->m_parentIndex = parent;
    obj->m_transform.SetParentID(parent);
    obj->m_childrenIndices.clear();

    if (!obj->m_tag.ToString().empty())
    {
        TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
    }

    if (!obj->m_layer.ToString().empty())
    {
        TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
    }

    auto parentObj = scene->m_SceneObjects[parent];
    if (parentObj && parentObj->m_index != newIndex)
    {
        if(std::find_if(parentObj->m_childrenIndices.begin(), parentObj->m_childrenIndices.end(),
            [&](GameObject::Index index) { return index == newIndex; }) == parentObj->m_childrenIndices.end())
        {
            parentObj->m_childrenIndices.push_back(newIndex);
        }
        else
        {
            Debug->LogWarning("GameObject with index " + std::to_string(newIndex) + " is already a child of " + std::to_string(parent));
		}

    }

    Benchmark bm2;
    if (node["m_components"])
    {
        for (const auto& componentNode : node["m_components"])
        {
            try
            {
                ComponentFactorys->LoadComponent(obj, componentNode ,true);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(e.what());
                continue;
            }
        }
    }
	std::cout << "Component Load Time: " << bm2.GetElapsedTime() << " ms\n";

    //if(SceneManagers->m_isGameStart)
    //{
    //    for (auto& comp : obj->m_components)
    //    {
    //        if (!comp) continue;

    //        auto script = std::dynamic_pointer_cast<ModuleBehavior>(comp);
    //        if (script)
    //        {
    //            ScriptManager->BindScriptEvents(script.get(), script->m_name.ToString());
    //        }
    //    }
    //}

    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            auto childObj = InstantiateRecursive(childNode, scene, obj->m_index);
        }
    }

    obj->m_prefab = const_cast<Prefab*>(this);
    obj->m_prefabFileGuid = GetFileGuid();
    obj->m_prefabOriginal = node;
    if (parent == 0)
        PrefabUtilitys->RegisterInstance(obj, this);

    return obj;
}