#include "PrefabUtility.h"
#include "DataSystem.h"
#include "GameObject.h"
#include "PrefabOverride.h"
#include "Scene.h"
#include "Object.h"
#include "ReflectionYml.h"
#include <cstring>
#include <unordered_set>
#include <unordered_map>

namespace
{
	// Entity 자체 프로퍼티(컴포넌트에 속하지 않는) 오버라이드 이름만 골라
	// Meta::DeserializePrefab에 넘길 제외 집합을 만든다.
	std::unordered_set<std::string> CollectGameObjectOverrideNames(const Entity& obj)
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
	void SeedOverridesFromSnapshot(Entity& obj)
	{
		if (!obj.m_prefabOriginal || !obj.m_prefabOriginal.IsMap())
			return;

		MetaYml::Node currentNode = Meta::Serialize(&obj, Meta::TypeOf<Entity>());

		SeedTypeOverrides(Meta::TypeOf<Entity>(), "", currentNode, obj.m_prefabOriginal, obj.m_prefabOverrides);

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

	// 컴포넌트 오버라이드 이름만 골라 Meta::DeserializePrefab에 넘길 제외 집합을
	// 만든다(componentType이 일치하는 것만). PrefabOverride 자체엔 순번이 없어
	// 이름 하나로 그 타입 전체를 가리킨다 — ApplyComponentDiff가 패치 대상
	// 컴포넌트를 파괴하지 않는 한 그래도 문제가 안 된다(아래 주석).
	std::unordered_set<std::string> CollectComponentOverrideNames(const Entity& obj, const std::string& componentTypeName)
	{
		std::unordered_set<std::string> names;
		for (const auto& ov : obj.m_prefabOverrides)
		{
			if (ov.m_componentType == componentTypeName)
				names.insert(ov.m_propertyName);
		}
		return names;
	}

	// 프리팹 갱신을 차집합으로 적용한다 (SceneGraphRedesignPlan P3 — Destroy 후
	// 재생성이던 P-f를 대체).
	//
	// 예전에는 obj.m_components를 전부 Destroy()한 뒤 프리팹 데이터로 처음부터
	// 다시 만들었다(P-f). 다른 시스템이 캐시해 둔 컴포넌트 포인터가 갱신마다
	// 통째로 무효화돼 플레이 중 갱신이 사실상 불가능했다. 이제 타입 단위로
	// 대조해 양쪽에 있는 타입은 인스턴스를 살려 둔 채 프로퍼티만 패치하고,
	// 프리팹에서 사라진 타입만 파괴하고 새로 추가된 타입만 만든다.
	//
	// 동일 타입이 여럿(스크립트 등)이면 타입+순번으로 식별한다 — 프리팹 데이터의
	// n번째 X는 인스턴스의 n번째 X에 패치된다(기존 컴포넌트 쪽 순서는
	// obj.m_components 원래 순서, 새 데이터 쪽 순서는 newComponents 순서). 개수가
	// 늘면 초과분만 새로 만들고, 줄면 초과분만 파괴한다.
	//
	// 재생성이 사라진 만큼 옛 ReapplyComponentOverrides(값 재적용)도 필요 없어졌다
	// — 패치 대상 컴포넌트는 애초에 파괴되지 않으므로, 오버라이드된 프로퍼티는
	// DeserializePrefab의 제외 목록에 걸려 새 값을 아예 받지 않고 지금 값 그대로
	// 남는다. "재적용"이 아니라 "안 건드림"으로 같은 결과를 얻는다.
	void ApplyComponentDiff(Entity& obj, const MetaYml::Node& newComponents)
	{
		const size_t originalCount = obj.m_components.size();

		// 타입 이름별로 기존 컴포넌트의 인덱스를 원래 순서대로 큐잉한다. 기존
		// 쪽은 FindTypeByInstance가 실제 런타임 typeID로 조회하므로 이름 충돌
		// 걱정이 없다(K1-b UUID·이름 폴백은 newComponents 쪽 ExtractTypeFromYAML
		// 에서만 필요하다).
		std::unordered_map<std::string, std::vector<size_t>> existingByType;
		for (size_t i = 0; i < originalCount; ++i)
		{
			const auto& comp = obj.m_components[i];
			if (!comp)
				continue;

			if (const Meta::Type* type = Meta::FindTypeByInstance(comp.get()))
				existingByType[type->name].push_back(i);
		}

		std::vector<bool> kept(originalCount, false);
		std::unordered_map<std::string, size_t> nextOrdinal;

		for (const auto& node : newComponents)
		{
			// K1-b UUID 우선, 이름 폴백 — Entity 레벨 갱신·ComponentFactory::
			// LoadComponent와 같은 판정 창구를 재사용한다.
			const Meta::Type* type = Meta::ExtractTypeFromYAML(node);
			if (!type)
				continue; // 타입을 못 정하면 LoadComponent도 이 노드를 버린다(로그는 그쪽 몫)

			auto& indices = existingByType[type->name];
			size_t& ordinal = nextOrdinal[type->name];

			if (ordinal < indices.size())
			{
				// 유지: 인스턴스 보존 + 프로퍼티 패치. 이 컴포넌트는 파괴 대상에서 뺀다.
				const size_t index = indices[ordinal++];
				kept[index] = true;

				Component* comp = obj.m_components[index].get();
				const auto overriddenNames = CollectComponentOverrideNames(obj, type->name);
				Meta::DeserializePrefab(comp, *type, node, overriddenNames);
			}
			else
			{
				// 추가: 프리팹 쪽 개수가 늘어난 초과분만 새로 만든다.
				++ordinal;
				try
				{
					ComponentFactorys->LoadComponent(&obj, node);
				}
				catch (const std::exception& e)
				{
					Debug->LogError(e.what());
				}
			}
		}

		// 제거: 프리팹에서 빠진 타입, 또는 개수가 줄어 남은 초과분만 파괴한다.
		// Destroy → OnDestroy → UnregisterComponent 순서는 옛 전량 재생성 경로와
		// 동일하게 유지한다 — UnregisterComponent를 먼저 하지 않으면 다음 프레임
		// 디스패치가 죽은 포인터를 순회한다(K2 §6 함정, "제거가 비로소 성립").
		Scene* scene = obj.GetScene();
		for (size_t i = 0; i < originalCount; ++i)
		{
			if (kept[i])
				continue;

			auto& comp = obj.m_components[i];
			if (!comp)
				continue;

			// ★ Transform은 절대 파괴하지 않는다 (S1-b).
			//
			// 이 루프의 규칙은 "소스 데이터의 m_components에 없으면 사용자가 지운 것"인데,
			// Transform은 그 규칙의 예외다 — 모든 GameObject가 생성자에서 무조건 갖고
			// (Entity.cpp) 캐시 포인터 m_pTransformComponent가 그것을 가리키므로,
			// 파괴되면 이후 Transform_() 호출이 전부 널 역참조다.
			// 구형식(마이그레이션 전) 프리팹 데이터는 Transform이 m_components에 없고
			// 최상위 m_transform 키에 있었다 — 그런 데이터가 이 함수에 흘러들면
			// 정확히 그 사고가 난다. 현재 유일한 호출부(PrefabEditor)는 살아있는
			// 오브젝트를 즉석 재직렬화해 넘기므로 신형식만 들어오지만, 그 전제는
			// 이 함수가 아니라 호출부에 있다 — 여기서 타입으로 못 박아 둔다.
			if (comp->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<Transform>())
				continue;

			// 축소 통지를 Scene::FlushPendingDestroy와 같은 순서로 맞춘다(트랙 L1).
			// 여기가 저장소에서 컴포넌트를 **즉시** 소멸시키는 유일한 경로다
			// (Entity::RemoveComponent조차 마크만 하고 프레임 끝 압축에 맡긴다).
			// 그래서 OnRemovingFromScene을 빠뜨리면, 그 훅에서 자기를 떼는
			// 시스템(C3의 AnimatorSystem 등)이 해제된 객체를 계속 들고 있게 된다 —
			// 프리팹 편집에서 Animator를 지우고 "인스턴스에 적용"하면 다음 프레임에
			// 죽은 포인터를 틱하는 경로였다.
			comp->Destroy();
			comp->OnEndSimulation();
			comp->OnRemovingFromScene();
			comp->OnUninitializing();   // L3: 옛 OnDestroy — 브리지 철거로 이름이 정본으로 바뀌었다
			if (scene)
				scene->UnregisterComponent(comp.get());
			comp.reset();
		}
		std::erase_if(obj.m_components, [](const std::unique_ptr<Component>& c) { return c == nullptr; });

		// m_componentIds·m_componentTypeMask를 한 번에 다시 세운다 — GameObject의
		// 기존 공개 API(내부에서 RebuildComponentTypeMask도 함께 부른다). 파괴분만큼
		// 인덱스가 당겨졌으므로 부분 갱신보다 전체 재구축이 더 안전하다.
		obj.RefreshComponentIdIndices();
	}
}

Prefab* PrefabUtility::CreatePrefab(const Entity* source, std::string_view name)
{
    Prefab* created = Prefab::CreateFromGameObject(source, name);
    if (!created)
        return nullptr;

    m_createdPrefabs.emplace_back(created);
    return created;
}

size_t PrefabUtility::RegisteredInstanceCount() const
{
    // 살아있는 것만 센다(P2) — 세대가 어긋난 항목은 다음 번 UpdateInstances가
    // 훑을 때 지워지지만, 그 전에 이 값을 물으면 죽은 항목까지 세게 된다.
    // 진단용 집계라 정확도가 곧 신뢰도다.
    size_t total = 0;
    for (const auto& [guid, instances] : m_instanceMap)
    {
        for (const auto& ref : instances)
        {
            if (Resolve(ref))
                ++total;
        }
    }
    return total;
}

Entity* PrefabUtility::Resolve(const InstanceRef& ref)
{
    if (!ref.scene || !ref.handle.IsValid())
        return nullptr;

    // ForgetScene이 못 따라잡는 경로(예: PrefabEditor.cpp의 임시 편집 씬 — 소유
    // 파일 밖이라 그쪽에서 ForgetScene을 부르게 배선할 수 없다)가 있어, Scene*를
    // 바로 역참조하기 전에 SceneManager가 들고 있는 생존 씬 목록으로 먼저
    // 확인한다. 포인터 값 비교만 하고 역참조는 하지 않으므로 댕글링 걱정이 없다.
    const auto& liveScenes = SceneManagers->GetScenes();
    if (std::find(liveScenes.begin(), liveScenes.end(), ref.scene) == liveScenes.end())
        return nullptr;

    return ref.scene->Resolve(ref.handle);
}

void PrefabUtility::ForgetScene(const Scene* scene)
{
    if (!scene)
        return;

    for (auto& [guid, instances] : m_instanceMap)
    {
        std::erase_if(instances, [&](const InstanceRef& ref) { return ref.scene == scene; });
    }
}

size_t PrefabUtility::OwnedPrefabCount() const
{
    return m_prefabCache.size() + m_createdPrefabs.size();
}

Entity* PrefabUtility::InstantiatePrefab(const Prefab* prefab, std::string_view name)
{
    if (!prefab)
        return nullptr;
    Entity* obj = prefab->Instantiate(name);
    if (obj)
    {
        obj->m_prefab = const_cast<Prefab*>(prefab);
        obj->m_prefabFileGuid = prefab->GetFileGuid();
        RegisterInstance(obj, prefab);
    }
    return obj;
}

Entity* PrefabUtility::InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name)
{
    if (!prefab || !targetScene)
        return nullptr;
    Entity* obj = prefab->Instantiate(targetScene, name);
    if (obj)
    {
        obj->m_prefab = const_cast<Prefab*>(prefab);
        obj->m_prefabFileGuid = prefab->GetFileGuid();
        RegisterInstance(obj, prefab);
    }
	return obj;
}

void PrefabUtility::RegisterInstance(Entity* instance, const Prefab* prefab)
{
    if (!instance || !prefab)
        return;

    Scene* scene = instance->GetScene();
    if (!scene)
        return; // 씬에 속하지 않은 오브젝트는 EntityHandle을 얻을 수 없다.

    const EntityHandle handle = scene->HandleOf(instance->m_index);
    if (!handle.IsValid())
        return;

    auto& instances = m_instanceMap[prefab->GetFileGuid()];
    const bool alreadyRegistered = std::any_of(instances.begin(), instances.end(),
        [&](const InstanceRef& ref) { return ref.scene == scene && ref.handle == handle; });
    if (alreadyRegistered)
        return; // 이미 등록된 인스턴스는 중복 저장하지 않는다

    instances.push_back({ scene, handle });
}

void PrefabUtility::UnregisterInstance(Entity* instance)
{
    if (!instance)
        return;

    Scene* scene = instance->GetScene();
    if (!scene)
        return; // 씬 밖 오브젝트는 애초에 핸들로 등록될 수 없었다.

    const EntityHandle handle = scene->HandleOf(instance->m_index);

    // 어느 프리팹의 인스턴스인지 알아도 목록 전체를 훑는다 — m_prefabFileGuid가
    // 이미 지워졌거나 등록 시점과 달라진 경우에도 항목을 남기지 않기 위해서다.
    // (세대 검사가 다음 읽기에서 걸러주긴 하지만, 즉시 지우는 편이 더 싸다 — P2)
    for (auto& [guid, instances] : m_instanceMap)
    {
        std::erase_if(instances, [&](const InstanceRef& ref)
        {
            return ref.scene == scene && ref.handle == handle;
        });
    }
}

void PrefabUtility::UpdateInstances(const Prefab* prefab)
{
    auto it = m_instanceMap.find(prefab->GetFileGuid());
    if (it == m_instanceMap.end())
        return;

    // 세대 검사로 죽은 인스턴스를 자동 걸러낸다(P0의 수동 erase 방어를 대체 —
    // SceneGraphRedesignPlan P2). 훑는 김에 목록에서도 지운다.
    std::erase_if(it->second, [](const InstanceRef& ref) { return Resolve(ref) == nullptr; });

    for (const InstanceRef& ref : it->second)
    {
        Entity* obj = Resolve(ref);
        if (!obj)
            continue;

        auto newData = prefab->GetPrefabData();

        // 명시 오버라이드가 비어 있으면(과도기) 적용 직전에 한 번만 시딩한다.
        if (obj->m_prefabOverrides.empty())
            SeedOverridesFromSnapshot(*obj);

        // Entity 자체 프로퍼티: 오버라이드된 것만 새 값 적용에서 제외한다(P-b·P-d 해소).
        Meta::DeserializePrefab(obj, newData, CollectGameObjectOverrideNames(*obj));

        // 역직렬화는 리플렉션으로 position/rotation/scale을 **직접** 써 넣는다 —
        // Transform의 세터를 거치지 않으므로 dirty가 서지 않는다. 새로 만든
        // 오브젝트는 스토어 슬롯 기본값이 dirty=1이라 무해했지만, 여기는 이미
        // 살아 있는 인스턴스를 덮어쓰는 자리다. S2의 dirty 게이트가 서기 전에는
        // 순회가 어차피 전량 재계산이라 드러나지 않던 구멍이다.
        obj->Transform_().SetDirty();

        // 컴포넌트: 통짜 Dump 비교로 갱신 여부를 정하던 것(P-e)을 걷어낸 자리에
        // Destroy 후 재생성(P-f)까지 걷어낸다(P3) — 차집합 적용으로 유지 타입은
        // 인스턴스를 보존한 채 패치하고, 사라진 타입만 파괴, 추가된 타입만 생성한다.
        if (newData["m_components"])
        {
            ApplyComponentDiff(*obj, newData["m_components"]);
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
