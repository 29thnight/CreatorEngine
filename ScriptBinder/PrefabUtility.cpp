#include "PrefabUtility.h"
#include "GameObject.h"
#include "Object.h"

Prefab* PrefabUtility::CreatePrefab(const GameObject* source, const std::string_view& name)
{
    return Prefab::CreateFromGameObject(source, name);
}

GameObject* PrefabUtility::InstantiatePrefab(const Prefab* prefab, const std::string_view& name)
{
    if (!prefab)
        return nullptr;
    GameObject* obj = prefab->Instantiate(name);
    if (obj)
    {
        obj->m_prefab = const_cast<Prefab*>(prefab);
        obj->m_prefabFileGuid = prefab->GetFileGuid();
        RegisterInstance(obj, prefab);
    }
    return obj;
}

void PrefabUtility::RegisterInstance(GameObject* instance, const Prefab* prefab)
{
    if (!instance || !prefab)
        return;
    m_instanceMap[prefab->GetFileGuid()].push_back(instance);
}

void PrefabUtility::UpdateInstances(const Prefab* prefab)
{
    auto it = m_instanceMap.find(prefab->GetFileGuid());
    if (it == m_instanceMap.end())
        return;
    for (GameObject* obj : it->second)
    {
        if (!obj)
            continue;
        auto savedPos = obj->m_transform.position;
        HashedGuid newInstanceID = obj->GetInstanceID();
        HashingString newHashedName = obj->GetHashedName();
        GameObject::Index newIndex = obj->m_index;
		GameObject::Index parentIndex = obj->m_parentIndex;
		auto children = obj->m_childrenIndices;
        Meta::Deserialize(obj, prefab->GetPrefabData());
        if (prefab->GetPrefabData()["m_components"])
        {
            for (auto comp : obj->m_components)
            {
                comp->Destroy();
				auto eventReceiver = std::dynamic_pointer_cast<IRegistableEvent>(comp);
                if (eventReceiver)
                {
                    eventReceiver->OnDestroy();
				}
            }

            obj->m_components.clear();
            obj->m_componentIds.clear();
            for (const auto& componentNode : prefab->GetPrefabData()["m_components"])
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
        obj->m_transform.position = savedPos;
        obj->m_instanceID = newInstanceID;
        obj->m_name = newHashedName;
		obj->m_index = newIndex;
		obj->m_parentIndex = parentIndex;
		obj->m_childrenIndices = children;
		obj->m_prefab = const_cast<Prefab*>(prefab);

        prefabInstanceUpdated.Broadcast(*obj);
    }
}

bool PrefabUtility::SavePrefab(const Prefab* prefab, const std::string& path)
{
    if (!prefab || path.empty())
        return false;
    std::ofstream out(path);
    if (!out.is_open())
        return false;

    auto node = Meta::Serialize(const_cast<Prefab*>(prefab));
	node["PrefabNode"].push_back(prefab->GetPrefabData());
    out << node;
    out.close();

    return true;
}

Prefab* PrefabUtility::LoadPrefab(const std::string& path)
{
    if (path.empty() || !file::exists(path))
        return nullptr;
    auto node = MetaYml::LoadFile(path);
    auto prefab = new Prefab();
    Meta::Deserialize(prefab, node);
	prefab->SetPrefabData(node["PrefabNode"]);
    prefab->SetFileGuid(make_file_guid(path));
    return prefab;
}