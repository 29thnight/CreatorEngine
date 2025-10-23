#include "PrefabUtility.h"
#include "GameObject.h"
#include "Object.h"
#include "ReflectionYml.h"

Prefab* PrefabUtility::CreatePrefab(const GameObject* source, std::string_view name)
{
    return Prefab::CreateFromGameObject(source, name);
}

GameObject* PrefabUtility::InstantiatePrefab(const Prefab* prefab, std::string_view name)
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

	auto& instances = m_instanceMap[prefab->GetFileGuid()];
    if(std::find(instances.begin(), instances.end(), instance) != instances.end())
    {
        return; // ̹ ϵ νϽ ߺ  
    }
    else
    {
        instances.push_back(instance);
    }
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

        auto newData = prefab->GetPrefabData();
        MetaYml::Node currentNode = Meta::Serialize(obj, GameObject::Reflect());
        bool updateComponents = false;

        if (newData["m_components"])
        {
            const auto& currComp = currentNode["m_components"];
            const auto& prevComp = obj->m_prefabOriginal["m_components"];
            if (currComp && prevComp && MetaYml::Dump(currComp) == MetaYml::Dump(prevComp))
                updateComponents = true;
        }

        Meta::DeserializePrefab(obj, newData, obj->m_prefabOriginal);

        if (updateComponents && newData["m_components"])
        {
            for (auto& comp : obj->m_components)
            {
                comp->Destroy();
                auto eventReceiver = std::dynamic_pointer_cast<IRegistableEvent>(comp);
                if (eventReceiver)
                    eventReceiver->OnDestroy();
            }
            obj->m_components.clear();
            obj->m_componentIds.clear();
            for (const auto& componentNode : newData["m_components"])
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
        obj->m_prefab = const_cast<Prefab*>(prefab);
        obj->m_prefabFileGuid = prefab->GetFileGuid();

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

Prefab* PrefabUtility::LoadPrefabFullPath(const std::string& path)
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

Prefab* PrefabUtility::LoadPrefab(const std::string& path)
{
    file::path filepath = PathFinder::Relative("Prefabs\\") / (path + ".prefab");
    if (!file::exists(filepath))
		return nullptr;

    auto prefab = LoadPrefabFullPath(filepath.string());
    if (prefab)
    {
        prefab->SetFileGuid(DataSystems->GetFileGuid(filepath.string()));
        return prefab;
    }

	return nullptr;
}
