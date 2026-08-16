#include "PrefabUtility.h"
#include "DataSystem.h"
#include "GameObject.h"
#include "PrefabOverride.h"
#include "Scene.h"
#include "Object.h"
#include "ReflectionYml.h"
#include <cstring>
#include <unordered_set>

namespace
{
	// GameObject 자체 프로퍼티(컴포넌트에 속하지 않는) 오버라이드 이름만 골라
	// Meta::DeserializePrefab에 넘길 제외 집합을 만든다.
	std::unordered_set<std::string> CollectGameObjectOverrideNames(const GameObject& obj)
	{
		std::unordered_set<std::string> names;
		for (const auto& ov : obj.m_prefabOverrides)
		{
			if (ov.m_componentType.empty())
				names.insert(ov.m_propertyName);
		}
		return names;
	}

	// type(및 type.parent 체인)의 프로퍼티를 스냅샷과 비교해 달라진 것만 오버라이드로
	// 시딩한다. componentType이 비어 있으면 GameObject 자신의 프로퍼티를 뜻한다.
	void SeedTypeOverrides(const Meta::Type& type, const std::string& componentType,
		const MetaYml::Node& currentNode, const MetaYml::Node& snapshotNode,
		std::vector<PrefabOverride>& out)
	{
		if (type.parent)
			SeedTypeOverrides(*type.parent, componentType, currentNode, snapshotNode, out);

		for (const auto& prop : type.properties)
		{
			// m_components·m_prefabOverrides 자신은 구조적으로 따로 다룬다 — 통짜
			// Dump 비교로 여기서 오버라이드로 잡으면 안 된다.
			if (componentType.empty() &&
				(std::strcmp(prop.name, "m_components") == 0 || std::strcmp(prop.name, "m_prefabOverrides") == 0))
				continue;

			const auto& currProp = currentNode[prop.name];
			const auto& snapProp = snapshotNode[prop.name];
			if (!currProp || !snapProp)
				continue;
			if (YAML::Dump(currProp) == YAML::Dump(snapProp))
				continue;

			PrefabOverride ov;
			ov.m_componentType = componentType;
			ov.m_propertyName = prop.name;
			ov.m_valueYaml = YAML::Dump(currProp);
			out.push_back(std::move(ov));
		}
	}

	// 과도기 시딩 (SceneGraphRedesignPlan P1 "과도기 시딩 허용").
	//
	// 오버라이드 기록의 정본은 에디터가 프로퍼티를 바꾸는 시점에 m_prefabOverrides에
	// 직접 쓰는 것이어야 하지만, 그 배선은 후속 슬라이스다. 그때까지는 목록이 비어
	// 있는 인스턴스(P1 이전에 만들어졌거나 정말로 손댄 적이 없는 인스턴스)를 만났을
	// 때 UpdateInstances 적용 직전에 현재 값과 obj.m_prefabOriginal(마지막으로 알려진
	// 프리팹 스냅샷)을 1회 비교해 목록을 채운다. 스냅샷이 비어 있으면(씬 재로드
	// 직후처럼 — m_prefabOriginal은 비직렬화다) 시딩할 근거가 없으니 아무것도 하지
	// 않는다 — 그 결과는 "오버라이드 없음"으로, 예외 1의 읽기 호환과 같다.
	void SeedOverridesFromSnapshot(GameObject& obj)
	{
		if (!obj.m_prefabOriginal || !obj.m_prefabOriginal.IsMap())
			return;

		MetaYml::Node currentNode = Meta::Serialize(&obj, GameObject::Reflect());

		SeedTypeOverrides(GameObject::Reflect(), "", currentNode, obj.m_prefabOriginal, obj.m_prefabOverrides);

		const auto& currComponents = currentNode["m_components"];
		const auto& snapComponents = obj.m_prefabOriginal["m_components"];
		if (currComponents && snapComponents && currComponents.IsSequence() && snapComponents.IsSequence())
		{
			const size_t count = std::min(currComponents.size(), snapComponents.size());
			for (size_t i = 0; i < count; ++i)
			{
				const Meta::Type* compType = Meta::ExtractTypeFromYAML(currComponents[i]);
				if (!compType)
					continue;
				SeedTypeOverrides(*compType, compType->name, currComponents[i], snapComponents[i], obj.m_prefabOverrides);
			}
		}
	}

	// Destroy 후 재생성(P-f, P3 소관)으로 사라진 컴포넌트 지역 값을 되먹인다.
	// 같은 타입이 여럿이면(스크립트 등) 전부에 적용한다 — 인스턴스 단위 식별은
	// P3(비파괴 갱신)에서 다룬다.
	void ReapplyComponentOverrides(GameObject& obj)
	{
		for (const auto& ov : obj.m_prefabOverrides)
		{
			if (ov.m_componentType.empty())
				continue; // GameObject 자체 프로퍼티 — DeserializePrefab에서 이미 제외됨

			for (auto& comp : obj.m_components)
			{
				if (!comp)
					continue;

				const Meta::Type* compType = Meta::FindTypeByInstance(comp.get());
				if (!compType || compType->name != ov.m_componentType)
					continue;

				MetaYml::Node wrapper;
				wrapper[ov.m_propertyName] = YAML::Load(ov.m_valueYaml);
				Meta::Deserialize(comp.get(), *compType, wrapper);
			}
		}
	}
}

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

        // 명시 오버라이드가 비어 있으면(과도기) 적용 직전에 한 번만 시딩한다.
        if (obj->m_prefabOverrides.empty())
            SeedOverridesFromSnapshot(*obj);

        // GameObject 자체 프로퍼티: 오버라이드된 것만 새 값 적용에서 제외한다(P-b·P-d 해소).
        Meta::DeserializePrefab(obj, newData, CollectGameObjectOverrideNames(*obj));

        // 컴포넌트: 통짜 Dump 비교로 갱신 여부를 정하던 것(P-e)을 걷어내고 항상
        // 새로 받는다 — Destroy 후 재생성 구조(P-f) 자체는 P3 소관이라 그대로 두되,
        // 재생성 직후 오버라이드된 (컴포넌트 타입, 프로퍼티) 값만 되먹여 지역 변경을
        // 보존한다.
        if (newData["m_components"])
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
            // K1-a 후속 배선: 마스크도 함께 비운다 — 안 하면 갱신으로 컴포넌트
            // 타입 수가 줄어들 때 이전 타입 비트가 남아 HasComponent가 거짓양성.
            obj->m_componentTypeMask = 0;
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

            ReapplyComponentOverrides(*obj);
        }
        obj->m_prefab = const_cast<Prefab*>(prefab);
        obj->m_prefabFileGuid = prefab->GetFileGuid();

        // 다음 시딩(과도기 한정)이 기준으로 삼을 스냅샷을 갱신한다. m_prefabOverrides가
        // 이미 정본이므로 이 값은 오버라이드 판정에 더 이상 쓰이지 않는다.
        obj->m_prefabOriginal = newData;

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
