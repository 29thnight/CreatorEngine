#include "PrefabUtility.h"
#include "DataSystem.h"
#include "GameObject.h"
#include "Scene.h"
#include "Object.h"
#include "ReflectionYml.h"

Prefab* PrefabUtility::CreatePrefab(const GameObject* source, std::string_view name)
{
    Prefab* created = Prefab::CreateFromGameObject(source, name);
    if (!created)
        return nullptr;

    m_createdPrefabs.emplace_back(created);
    return created;
}

size_t PrefabUtility::RegisteredInstanceCount() const
{
    size_t total = 0;
    for (const auto& [guid, instances] : m_instanceMap)
    {
        total += instances.size();
    }
    return total;
}

size_t PrefabUtility::OwnedPrefabCount() const
{
    return m_prefabCache.size() + m_createdPrefabs.size();
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

void PrefabUtility::UnregisterInstance(GameObject* instance)
{
    if (!instance)
        return;

    // 어느 프리팹의 인스턴스인지 알아도 목록 전체를 훑는다 — m_prefabFileGuid가
    // 이미 지워졌거나 등록 시점과 달라진 경우에도 죽은 포인터를 남기지 않기 위해서다.
    for (auto& [guid, instances] : m_instanceMap)
    {
        std::erase(instances, instance);
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

    // 캐시가 방금 덮어쓴 파일의 옛 내용을 들고 있으면, 저장한 뒤 다시 로드했을 때
    // 저장 전 상태가 돌아온다. 저장한 것과 다른 객체가 캐시에 있을 때만 버린다
    // (같은 객체를 지우면 호출자가 들고 있는 포인터가 죽는다).
    const std::string key = CacheKey(path);
    if (auto it = m_prefabCache.find(key);
        it != m_prefabCache.end() && it->second.get() != prefab)
    {
        m_prefabCache.erase(it);
    }

    return true;
}

Prefab* PrefabUtility::LoadPrefabFullPath(const std::string& path)
{
    if (path.empty() || !file::exists(path))
        return nullptr;

    const std::string key = CacheKey(path);
    if (auto it = m_prefabCache.find(key); it != m_prefabCache.end())
    {
        return it->second.get();
    }

    auto node = MetaYml::LoadFile(path);
    auto prefab = std::make_unique<Prefab>();
    Meta::Deserialize(prefab.get(), node);
	prefab->SetPrefabData(node["PrefabNode"]);
    prefab->SetFileGuid(make_file_guid(path));

    Prefab* raw = prefab.get();
    m_prefabCache.emplace(key, std::move(prefab));
    return raw;
}

std::string PrefabUtility::CacheKey(const std::string& path)
{
    // 같은 파일을 상대·절대 경로로 각각 열면 다른 항목이 되어 캐시가 무의미해진다.
    std::error_code ec;
    const file::path canonical = file::weakly_canonical(file::path(path), ec);
    std::string key = ec ? path : canonical.string();
    std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
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
