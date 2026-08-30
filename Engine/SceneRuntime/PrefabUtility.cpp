#include "PrefabUtility.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "DataSystem.h"
#include "Entity.h"
#include "PrefabOverride.h"
#include "Scene.h"
#include "Object.h"
#include "LifecycleTrace.h"
#include "AuthoringNodeEquality.h" // D3-a-1: 구조 비교
#include "AuthoringDocumentAccess.h"
#include "ReflectionYml.h"
#include "SerializationProfiler.h" // D0: 직렬화 기준선 계측
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace
{
	// Entity 자체 프로퍼티(컴포넌트에 속하지 않는) 오버라이드 이름만 골라
	// Meta::DeserializePrefab에 넘길 제외 집합을 만든다. m_index는 슬롯 정체성이므로
	// 항상 제외한다. 계층은 H3에서 Entity 리플렉션 필드가 아니어서 이 목록에조차
	// 들어오지 않고, HierarchyStore topology가 그대로 보존된다.
	std::unordered_set<std::string> CollectGameObjectOverrideNames(const Entity& obj)
	{
		std::unordered_set<std::string> names{ "m_index" };
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
		std::vector<PrefabOverride>& out, int componentSlot = -1)
	{
		if (type.parent)
			SeedTypeOverrides(*type.parent, componentType, currentNode, snapshotNode, out, componentSlot);

		for (const auto& prop : type.properties)
		{
			// m_components·m_prefabOverrides 자신은 구조적으로 따로 다룬다 — 통짜
			// Dump 비교로 여기서 오버라이드로 잡으면 안 된다.
			if (componentType.empty() &&
				(std::strcmp(prop.name, "m_components") == 0
					|| std::strcmp(prop.name, "m_prefabOverrides") == 0
					|| std::strcmp(prop.name, "m_index") == 0))
				continue;

			const auto& currProp = currentNode[prop.name];
			const auto& snapProp = snapshotNode[prop.name];
			if (!currProp || !snapProp)
				continue;
			// D3-a-1: 문자열 덤프 비교 → 구조 비교(§3.3, Y-6). 비교하려고 문자열을
			// 만들지 않고, 맵 키 순서 같은 표기 차이를 오버라이드로 오인하지 않는다.
			// 아래 `ov.m_valueYaml`은 **저장 표현**이라 그대로 둔다 — 그것을 바꾸면
			// 파일 포맷이 바뀐다(§1.7 ⑧).
			if (Authoring::NodesEqual(currProp, snapProp))
				continue;

			PrefabOverride ov;
			ov.m_componentType = componentType;
			ov.m_componentSlot = componentSlot;
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
		if (obj.m_prefabOriginal.IsEmpty())
			return;
		const MetaYml::Node& snapshotNode =
			Authoring::DocumentAccess::Node(obj.m_prefabOriginal);
		if (!snapshotNode.IsMap())
			return;

		MetaYml::Node currentNode = Meta::Serialize(&obj, Meta::TypeOf<Entity>());

		SeedTypeOverrides(Meta::TypeOf<Entity>(), "", currentNode, snapshotNode, obj.m_prefabOverrides);

		const auto& currComponents = currentNode["m_components"];
		const auto& snapComponents = snapshotNode["m_components"];
		if (currComponents && snapComponents && currComponents.IsSequence() && snapComponents.IsSequence())
		{
			// 순번은 **타입별 누적**이다 — 절대 인덱스가 아니다.
			// ApplyComponentDiff의 nextOrdinal과 같은 규칙이어야 하고, 그쪽도
			// obj.m_components 순서로 센다(직렬화 순서가 곧 그 순서다).
			std::unordered_map<std::string, int> slotByType;
			const size_t count = std::min(currComponents.size(), snapComponents.size());
			for (size_t i = 0; i < count; ++i)
			{
				const Meta::Type* compType = Meta::ExtractTypeFromYAML(currComponents[i]);
				if (!compType)
					continue;
				const int slot = slotByType[compType->name]++;
				SeedTypeOverrides(*compType, compType->name, currComponents[i], snapComponents[i],
					obj.m_prefabOverrides, slot);
			}
		}
	}

	// 컴포넌트 오버라이드 이름만 골라 Meta::DeserializePrefab에 넘길 제외 집합을
	// 만든다 — 타입 이름이 같고 **순번도 맞는** 것만 (P4-c).
	//
	// slot < 0인 기록은 "타입 전체"를 뜻한다(순번 필드가 없던 시절 데이터).
	// 그 경우 예전 행동 그대로 그 타입의 모든 컴포넌트에 걸린다.
	std::unordered_set<std::string> CollectComponentOverrideNames(
		const Entity& obj, const std::string& componentTypeName, int slot)
	{
		std::unordered_set<std::string> names;
		for (const auto& ov : obj.m_prefabOverrides)
		{
			if (ov.m_componentType != componentTypeName)
				continue;
			// 순번 미지정(-1)은 타입 전체 — 옛 데이터의 호환 경로다.
			if (ov.m_componentSlot >= 0 && ov.m_componentSlot != slot)
				continue;
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

				// 순번은 **정본 헬퍼로만** 센다. 여기서 ordinal-1로 직접 계산하면
				// 기록하는 쪽(시딩·RecordPropertyOverride)과 세는 자리가 둘로 갈리고,
				// 갈리는 순간 어긋남이 조용해진다 — 배제 집합이 엉뚱한 컴포넌트에
				// 걸려도 에러가 나지 않는다. 값은 ordinal-1과 같아야 하고, 다르면
				// 그것이 곧 결함이다.
				const auto overriddenNames = CollectComponentOverrideNames(
					obj, type->name, PrefabUtility::ComputeComponentSlot(obj, comp));
				Meta::DeserializePrefab(comp, *type, node, overriddenNames);
			}
			else
			{
				// 추가: 프리팹 쪽 개수가 늘어난 초과분만 새로 만든다.
				++ordinal;
				try
				{
					ComponentFactorys->LoadComponent(&obj, Authoring::NodeViewAccess::Make(node));
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
			// 이 축소 삼단도 기록한다(C5). 여기는 즉시 소멸 경로라 Scene의 프레임 끝
			// 정리와 순서가 다르고, 기록이 없으면 그 차이가 기준선에 안 보인다.
			LIFECYCLE_TRACE(Lifecycle::Phase::OnEndSimulation, Lifecycle::Trace::TypeNameOf(comp.get()),
				obj.m_name.ToString().c_str(), comp->GetInstanceID());
			comp->OnEndSimulation();
			LIFECYCLE_TRACE(Lifecycle::Phase::OnRemovingFromScene, Lifecycle::Trace::TypeNameOf(comp.get()),
				obj.m_name.ToString().c_str(), comp->GetInstanceID());
			comp->OnRemovingFromScene();
			LIFECYCLE_TRACE(Lifecycle::Phase::OnUninitializing, Lifecycle::Trace::TypeNameOf(comp.get()),
				obj.m_name.ToString().c_str(), comp->GetInstanceID());
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

int PrefabUtility::ComputeComponentSlot(const Entity& obj, const Component* target)
{
    if (!target)
        return -1;

    const Meta::Type* targetType = Meta::FindTypeByInstance(const_cast<Component*>(target));
    if (!targetType)
        return -1;

    int slot = 0;
    for (const auto& comp : obj.m_components)
    {
        if (!comp)
            continue;
        if (comp.get() == target)
            return slot;

        const Meta::Type* type = Meta::FindTypeByInstance(comp.get());
        if (type && type->name == targetType->name)
            ++slot;
    }
    return -1;
}

void PrefabUtility::ApplyRecordedOverrides(Entity& obj)
{
    if (obj.m_prefabOverrides.empty())
        return;

    // ★ Entity 자신의 프로퍼티(m_componentType이 빈 항목)는 **일부러 되먹이지
    // 않는다.** 과도기 시딩(SeedOverridesFromSnapshot)이 m_name·m_instanceID·
    // m_index·m_parentIndex 같은 **엔진 장부까지 사용자 수정으로 오기록**한
    // 이력이 있다(P-write 실측: 8건 중 7건이 identity 잡음). 그것을 갓 만든
    // 오브젝트에 되먹이면 인덱스와 계층이 그 자리에서 망가진다.
    //
    // 되먹일 필요도 없다 — 참조 노드는 Entity 필드를 그대로 싣는다(m_name·
    // m_tag 등). 담기지 않는 것은 컴포넌트뿐이고, 이 함수가 그 구멍만 메운다.
    for (const auto& comp : obj.m_components)
    {
        if (!comp)
            continue;

        Component* target = comp.get();
        const Meta::Type* type = Meta::FindTypeByInstance(target);
        if (!type)
            continue;

        // 순번은 정본 헬퍼로만 센다(P4-c) — 기록하는 쪽(RecordPropertyOverride)과
        // 같은 함수를 부르므로 규칙이 갈릴 수 없다.
        const int slot = ComputeComponentSlot(obj, target);

        MetaYml::Node patched;
        bool any = false;

        for (const auto& ov : obj.m_prefabOverrides)
        {
            if (ov.m_componentType != type->name)
                continue;
            // -1은 "순번 미지정 = 그 타입 전체"(PrefabOverride.h) — 옛 데이터 호환.
            if (ov.m_componentSlot != -1 && ov.m_componentSlot != slot)
                continue;
            if (ov.m_propertyName.empty() || ov.m_valueYaml.empty())
                continue;

            if (!any)
            {
                // 현재 형상에서 출발해 오버라이드된 프로퍼티만 덮는다 —
                // DeserializePrefab(배제 방식)의 정확한 거울상이다.
                patched = Meta::Serialize(target, *type);
                any = true;
            }

            try
            {
                patched[ov.m_propertyName] = MetaYml::Load(ov.m_valueYaml);
            }
            catch (const std::exception& e)
            {
                // 조용히 넘기지 않는다 — 값 하나가 유실되면 그 필드는 이후
                // 프리팹 갱신을 계속 받아 사용자 수정이 사라진 것처럼 보인다.
                Debug->LogError("ApplyRecordedOverrides: 값 파싱 실패 "
                    + type->name + "." + ov.m_propertyName + " — " + e.what());
            }
        }

        if (any)
            Meta::Deserialize(target, *type, patched);
    }
}

void PrefabUtility::RecordPropertyOverride(Entity& obj, const Component& component,
    const std::string& propertyName)
{
    // ── 게이트 ① 프리팹 인스턴스가 아니면 기록할 것이 없다 ──
    //
    // m_prefabFileGuid는 직렬화되므로 저장·재로드를 건넌다(m_prefabOriginal이
    // 비직렬화라 못 하던 일이 바로 이것이다).
    static const FileGuid nullGuid{};
    if (obj.m_prefabFileGuid == nullGuid)
        return;

    // ── 게이트 ② 재생 중의 값 변화는 저작 의도가 아니다 ──
    //
    // 게임플레이가 매 프레임 쓰는 값(체력·위치·상태)까지 오버라이드로 굳으면
    // 그 필드는 이후 프리팹 갱신을 영원히 못 받는다. 에디터가 들고 있는
    // 에디터 UndoManager의 m_isGameMode가 아니라 여기를 보는 이유는,
    // 그쪽은 에디터 UI가 채우는 미러라 Player 빌드에서 항상 false이기 때문이다
    // (그 싱글턴은 이제 에디터 층 전용이라 Core에서 보이지도 않는다).
    if (nullptr != SceneManagers && SceneManagers->IsGameStart())
        return;

    const Meta::Type* type = Meta::FindTypeByInstance(const_cast<Component*>(&component));
    if (!type)
        return;

    // 값은 지금 형상에서 뽑는다 — 시딩(SeedTypeOverrides)과 같은 패턴이라
    // 두 경로가 같은 문자열 형태를 남긴다.
    MetaYml::Node node = Meta::Serialize(const_cast<Component*>(&component), *type);
    const auto& propNode = node[propertyName];
    if (!propNode)
        return;

    // 순번은 정본 헬퍼로만 센다(P4-c). 소비하는 쪽(ApplyComponentDiff)도 같은
    // 함수를 부르므로 규칙이 갈릴 수 없다.
    const int slot = ComputeComponentSlot(obj, &component);
    const std::string valueYaml = YAML::Dump(propNode);

    for (auto& ov : obj.m_prefabOverrides)
    {
        if (ov.m_componentType == type->name
            && ov.m_componentSlot == slot
            && ov.m_propertyName == propertyName)
        {
            ov.m_valueYaml = valueYaml;   // 같은 프로퍼티를 또 만졌다 — 값만 갱신
            return;
        }
    }

    PrefabOverride ov;
    ov.m_componentType = type->name;
    ov.m_componentSlot = slot;
    ov.m_propertyName = propertyName;
    ov.m_valueYaml = valueYaml;
    obj.m_prefabOverrides.push_back(std::move(ov));
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
    // D0(SerializationPlan): 소환은 자기 자신이 루트다 — 씬 로드 분해 합에 포함하지 않는다.
    SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::PrefabInstantiate);
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
    // D0: 위 오버로드와 서로 부르지 않으므로 이중 계수가 아니다.
    SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::PrefabInstantiate);
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

		// Entity 자체 프로퍼티: 사용자 오버라이드와 슬롯 정체성은 새 값 적용에서
		// 제외한다. 계층은 H3부터 리플렉션 프로퍼티가 아니라 Store topology를 우회할
		// 경로 자체가 없다.
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
        //
        // ★ 여기도 깊은 복사다. newData는 prefab->GetPrefabData()의 **별칭**이라
        // (yaml-cpp 참조 의미) 그냥 대입하면 이 인스턴스의 스냅샷이 프리팹의 살아
        // 있는 데이터를 계속 가리킨다 — 프리팹이 다음에 바뀌면 스냅샷이 소리 없이
        // 따라 바뀌어 "무엇과 비교하는지"가 무너진다. 사유는 Prefab.cpp의 같은 자리.
        obj->m_prefabOriginal = Authoring::DocumentAccess::Adopt(MetaYml::Clone(newData));

        prefabInstanceUpdated.Broadcast(*obj);
    }
}

bool PrefabUtility::SavePrefab(const Prefab* prefab, const std::string& path)
{
    if (!prefab || path.empty())
        return false;

	// 이미 sidecar가 있으면 catalog GUID가 정본이다. 새 prefab만 호출자가 들고
	// 있던 UUIDv4를 승계하거나 여기서 발급하고, CreateMeta에 같은 값을 요청한다.
	// 경로/이름 해시는 영속 자산 정체성에 다시 들어오지 않는다.
	static const FileGuid nullGuid{};
	FileGuid identity = DataSystems->GetFileGuid(path);
	if (identity == nullGuid) identity = prefab->GetFileGuid();
	if (identity == nullGuid) identity = FileGuid::CreateRandomV4();
	const_cast<Prefab*>(prefab)->SetFileGuid(identity);

    auto node = Meta::Serialize(const_cast<Prefab*>(prefab));

	// Prefab::m_fileGuid는 sidecar 정체성의 payload mirror이고, 각 엔티티의
	// m_prefabFileGuid는 인스턴스 재연결용 참조다. 둘 다 같은 catalog GUID를
	// 기록하지만 정본은 sidecar 하나뿐이다.
    // 형상이 둘 다 나온다: SerializeRecursive는 루트 Map 하나를 돌려주고(자식은
    // 그 안에 중첩), 다른 경로에서 온 데이터는 Sequence다. 둘 다 각인한다 —
    // 한쪽만 처리하면 조용히 널로 남는다(실측으로 한 번 걸렸다).
    MetaYml::Node data = prefab->GetPrefabData();
    if (data.IsSequence())
    {
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            data[i]["m_prefabFileGuid"] = identity.ToString();
        }
    }
    else if (data.IsMap())
    {
        data["m_prefabFileGuid"] = identity.ToString();
    }

	node["PrefabNode"].push_back(data);
	std::ostringstream payload;
	payload << node;

	// Editor Host는 staging publish와 meta 확정을 같은 authoring lock 안에서 끝낸다.
	// Player에는 handler가 없으므로 source meta를 만들지 않고 기존 파일 쓰기만
	// 유지한다(실제 제품에서는 이 경로를 호출하지 않는다).
	const bool hasAuthoringHost = AssetAuthoringPort::IsInstalled();
	if (hasAuthoringHost)
	{
		const FileGuid authoredIdentity = AssetAuthoringPort::WriteTextAssetWithMeta(
			path, payload.str(), identity);
		if (authoredIdentity != identity)
		{
			Debug->LogError("Prefab/meta GUID mismatch after authoring: " + path);
			return false;
		}
	}
	else
	{
		// D3-b: 저작 텍스트는 LF로 쓴다. Windows의 텍스트 모드는 개행을 CRLF로 바꾼다.
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) return false;
		out << payload.str();
		out.flush();
		if (!out.good()) return false;
	}

	// Host authoring port가 감시자를 기다리지 않고 같은 transaction 안에서
	// RuntimeAssetChange::CatalogUpsert까지 적용한다. 그러지 않으면 같은 세션 안에서
	// LoadPrefab이 DataSystems->GetFileGuid로 널을
    // 받아, 방금 만든 프리팹의 인스턴스가 프리팹을 가리키지 못한다(실측: 같은
    // 시나리오가 첫 실행에서 "씬 인스턴스 0개", 파일이 남은 재실행에서 1개 —
    // 즉 결과가 **이전 실행의 잔재에 좌우된다**). 회귀 게이트가 그런 자에 기대면
    // 깨끗한 체크아웃의 첫 실행에서만 실패하는, 가장 찾기 어려운 종류가 된다.
	// 캐시가 방금 덮어쓴 파일의 옛 내용을 들고 있으면, 저장한 뒤 다시 로드했을 때
    // 저장 전 상태가 돌아온다.
    //
    // ★ erase가 아니라 **데이터 교체**다 (2026-08-20, P4-b에서 교정).
    //
    // 캐시 항목은 unique_ptr이라 지우면 그 Prefab 객체가 파괴되는데, 살아 있는
    // 인스턴스들이 `obj->m_prefab`으로 **그 객체를 raw 포인터로 들고 있다**
    // (Prefab::InstantiateRecursive가 넣는다). 지우는 순간 그 포인터가 전부 뜬다.
    // 객체를 살려 두고 정의만 새것으로 바꾸면 두 가지가 함께 풀린다 — 포인터가
    // 살고, 다음 LoadPrefabGuid/LoadPrefab이 새 정의를 돌려준다.
    //
    // 후자가 P4-b의 전제다: 중첩 프리팹 참조 노드는 **소환 시점에** 이 경로로
    // 정의를 다시 읽는다. 캐시가 옛 정의를 계속 주면 참조 노드로 굽어도 전파가
    // 일어나지 않는다(실측으로 정확히 그 모습이 나왔다 — 판정 4는 GREEN인데
    // 판정 6만 RED).
    //
    // 형상을 맞춘다: 캐시 객체의 m_prefabData는 LoadPrefabFullPath가 넣은
    // `node["PrefabNode"]`(Sequence)다. 방금 만든 것도 같은 노드를 쓴다.
    const std::string key = CacheKey(path);
    if (auto it = m_prefabCache.find(key);
        it != m_prefabCache.end() && it->second.get() != prefab)
    {
        // D3-a-5b: 뷰는 소유하지 않으므로 대상 노드에 이름을 준다.
        const MetaYml::Node cachedPrefabNode = node["PrefabNode"];
        it->second->SetPrefabData(Authoring::NodeViewAccess::Make(cachedPrefabNode));
        it->second->SetFileGuid(identity);
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

    // D0(SerializationPlan): 캐시 미스 구간 전체(파싱 + 정의 역직렬화 + identity 해석).
    // LoadFile만 재면 정의 역직렬화 몫이 빠져 "프리팹 로드 비용"을 과소 보고한다.
    SERIALIZATION_PROFILE_SCOPE(SerializationProfile::Stage::PrefabParse);

    auto node = MetaYml::LoadFile(path);
    auto prefab = std::make_unique<Prefab>();
    Meta::Deserialize(prefab.get(), node);
	const MetaYml::Node loadedPrefabNode = node["PrefabNode"];
	prefab->SetPrefabData(Authoring::NodeViewAccess::Make(loadedPrefabNode));
	const FileGuid identity = DataSystems->GetFileGuid(path);
	if (identity == FileGuid{})
	{
		Debug->LogError("Prefab load rejected missing catalog identity: " + path);
		return nullptr;
	}
	prefab->SetFileGuid(identity);

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
    return prefab;
}

Prefab* PrefabUtility::LoadPrefabGuid(const FileGuid& guid)
{
    auto path = DataSystems->GetFilePath(guid);
    if (path.empty())
        return nullptr;
	return LoadPrefabFullPath(path.string());
}
