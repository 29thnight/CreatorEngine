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

GameObject* PrefabUtility::InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name)
{
    if (!prefab || !targetScene)
        return nullptr;
    GameObject* obj = prefab->Instantiate(targetScene, name);
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
        return; // �̹� ��ϵ� �ν��Ͻ��� �ߺ� ������� ����
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
            if (currComp && prevComp && YAML::Dump(currComp) == YAML::Dump(prevComp))
                updateComponents = true;
        }

        Meta::DeserializePrefab(obj, newData, obj->m_prefabOriginal);

        if (updateComponents && newData["m_components"])
        {
            for (auto& comp : obj->m_components)
            {
                if (!comp) continue;

                comp->Destroy();

                // 훅이 Component로 온 뒤로 캐스트가 필요 없다(PHASE 9-1).
                //
                // 그리고 그 캐스트는 버그였다 — RegistableEvent를 상속하지 않은
                // 컴포넌트는 여기서 OnDestroy를 받지 못했다. 프리팹을 갱신할 때만
                // 도는 경로라 드러나기 어려웠다. 이제 전부 받는다.
                comp->OnDestroy();

                // 아래 clear로 사라질 것들을 디스패치 리스트에서 먼저 빼야 한다.
                // 남겨 두면 다음 프레임이 죽은 포인터를 순회한다.
                if (Scene* scene = obj->GetScene()) scene->UnregisterComponent(comp.get());
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

Prefab* PrefabUtility::LoadPrefabGuid(const FileGuid& guid)
{
    auto path = DataSystems->GetFilePath(guid);
    if (path.empty())
        return nullptr;
	return LoadPrefabFullPath(path.string());
}
