#include "Prefab.h"
#include "EntityAuthoringRead.h" // D3-a-2: 저작 읽기 어댑터
#include "AuthoringDocumentAccess.h"
#include "AuthoringNodeViewAccess.h" // D3-a-5b
#include "Entity.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "Scene.h"
#include <unordered_map>
#include <vector>

// 구파일 승격 공유 헬퍼(레인 2, SceneGraphRedesignPlan §5 예외 4) — 정의는
// SceneManager.cpp에 있다(그쪽 파일 상단 주석 참고). 헤더를 새로 두지 않기
// 위해(배정 파일 밖 편집 금지) 여기서 직접 forward-declare 한다.
namespace LegacyTransformPromotion
{
    void PromoteLegacyTransform(Entity* obj, const MetaYml::Node& node);
}

namespace
{
	struct PrefabNodeRecord
	{
		MetaYml::Node node;
		std::vector<uint32_t> path;
	};

	void CollectPrefabNodes(const MetaYml::Node& node, std::vector<uint32_t>& path,
		std::vector<PrefabNodeRecord>& records,
		std::unordered_map<size_t, std::vector<uint32_t>>& pathByLegacyId)
	{
		if (!node) return;
		records.push_back({ node, path });
		if (node["m_instanceID"])
		{
			pathByLegacyId[node["m_instanceID"].as<size_t>()] = path;
		}

		const MetaYml::Node children = node["children"];
		if (!children || !children.IsSequence()) return;
		for (uint32_t ordinal = 0; ordinal < children.size(); ++ordinal)
		{
			path.push_back(ordinal);
			CollectPrefabNodes(children[ordinal], path, records, pathByLegacyId);
			path.pop_back();
		}
	}

	// U7 구파일 승격 도구. 디스크를 직접 고치는 일괄 변환은 저작 자산 폐기로
	// 대상이 0개가 됐지만, 사용자가 보유한 옛 프리팹은 계속 열 수 있어야 한다.
	// 인스턴스화할 YAML 복사본에서 navObject(instanceID)를 source-relative route로
	// 바꾼다. 원본 m_prefabData는 건드리지 않으며, 인스턴스를 다음에 저장하면
	// UIComponent::OnBeforeSerialize가 새 형식만 쓴다.
	void UpgradeLegacyNavigation(MetaYml::Node& prefabData)
	{
		if (!prefabData || !prefabData.IsSequence()) return;

		for (uint32_t rootOrdinal = 0; rootOrdinal < prefabData.size(); ++rootOrdinal)
		{
			std::vector<PrefabNodeRecord> records;
			std::unordered_map<size_t, std::vector<uint32_t>> pathByLegacyId;
			std::vector<uint32_t> path;
			CollectPrefabNodes(prefabData[rootOrdinal], path, records, pathByLegacyId);

			for (PrefabNodeRecord& record : records)
			{
				const MetaYml::Node components = record.node["m_components"];
				if (!components || !components.IsSequence()) continue;

				for (auto componentNode : components)
				{
					MetaYml::Node navigations = componentNode["navigations"];
					if (!navigations || !navigations.IsSequence()) continue;

					for (auto navigationNode : navigations)
					{
						if (!navigationNode["navObject"] || navigationNode["parentHops"])
							continue;

						const size_t legacyTarget = navigationNode["navObject"].as<size_t>();
						const auto targetIt = pathByLegacyId.find(legacyTarget);
						if (targetIt == pathByLegacyId.end())
						{
							Debug->LogWarning("구 Navigation 링크를 프리팹 로컬 경로로 승격하지 못했다: 대상 instanceID "
								+ std::to_string(legacyTarget) + "가 같은 프리팹 트리에 없다");
							continue;
						}

						const std::vector<uint32_t>& targetPath = targetIt->second;
						size_t common = 0;
						while (common < record.path.size() && common < targetPath.size()
							&& record.path[common] == targetPath[common])
						{
							++common;
						}

						navigationNode["parentHops"] = static_cast<uint32_t>(record.path.size() - common);
						MetaYml::Node childOrdinals;
						childOrdinals.SetStyle(MetaYml::EmitterStyle::Flow);
						for (size_t i = common; i < targetPath.size(); ++i)
							childOrdinals.push_back(targetPath[i]);
						navigationNode["childOrdinals"] = childOrdinals;
						navigationNode.remove("navObject");
					}
				}
			}
		}
	}
}

// 프리팹 파일의 Entity 인덱스는 저작 Scene의 슬롯 번호다. 대상 Scene에 그대로
// 쓰면 특히 Bone의 m_rootIndex가 우연히 같은 번호의 다른 Entity를 가리킨다.
// 한 번의 인스턴스화가 만드는 트리 안에서 source slot -> target slot을 모은 뒤,
// 모든 노드가 생긴 시점에 root 참조를 고친다. 중첩 프리팹은 별도 컨텍스트를 써서
// 서로 다른 저작 Scene의 같은 슬롯 번호가 충돌하지 않게 한다.
struct Prefab::InstantiateContext
{
	struct RootFixup
	{
		Entity* entity{ nullptr };
		Entity::Index targetIndex{ Entity::INVALID_INDEX };
		Entity::Index sourceRootIndex{ Entity::INVALID_INDEX };
	};

	std::unordered_map<Entity::Index, Entity::Index> indexRemap;
	std::vector<RootFixup> rootFixups;

	void RecordIndex(Entity::Index sourceIndex, Entity::Index targetIndex)
	{
		if (!Entity::IsValidIndex(sourceIndex))
			return;

		const auto [it, inserted] = indexRemap.emplace(sourceIndex, targetIndex);
		if (!inserted && it->second != targetIndex)
		{
			Debug->LogWarning("Prefab 인스턴스화 중 중복 source index를 발견했다: "
				+ std::to_string(sourceIndex) + " (첫 매핑을 유지한다)");
		}
	}

	void RecordRoot(Entity* entity, Entity::Index targetIndex, Entity::Index sourceRootIndex)
	{
		rootFixups.push_back({ entity, targetIndex, sourceRootIndex });
	}

	void ApplyRootFixups(Scene* scene) const
	{
		if (!scene)
			return;

		for (const RootFixup& fixup : rootFixups)
		{
			if (!fixup.entity || scene->GetEntityRaw(fixup.targetIndex) != fixup.entity)
				continue;

			Entity::Index targetRootIndex = fixup.sourceRootIndex;
			if (Entity::IsValidIndex(fixup.sourceRootIndex))
			{
				if (const auto found = indexRemap.find(fixup.sourceRootIndex); found != indexRemap.end())
				{
					targetRootIndex = found->second;
				}
				else if (fixup.sourceRootIndex == Entity::kSceneRootIndex)
				{
					targetRootIndex = Entity::kSceneRootIndex;
				}
				else
				{
					// SceneManager의 load-batch remap과 같은 안전 폴백이다. 찾지 못한
					// source slot을 대상 Scene의 동번호 슬롯로 해석하지 않는다.
					targetRootIndex = fixup.targetIndex;
					Debug->LogWarning("Prefab root index를 대상 계층에서 찾지 못했다: source "
						+ std::to_string(fixup.sourceRootIndex) + ", self로 정규화한다");
				}
			}

			fixup.entity->SetRootIndex(targetRootIndex);
		}
	}
};

Prefab::Prefab(std::string_view name, const Entity* source)
    : Object(name)
{
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<Prefab>();
    if (source)
    {
        // ownerPrefabGuid는 **굽기 원본 루트의 guid**다(P4-b). 원본이 다른
        // 프리팹의 인스턴스면 그 guid이고, 평범한 오브젝트면 널이다. 자식의
        // guid가 이 값과 다르고 유효하면 중첩 인스턴스 루트로 판정한다.
        //
        // 이 프리팹 자신의 FileGuid를 쓰지 않는 이유: 생성자 시점에는 아직
        // 발급 전이다(SavePrefab이 매긴다 — P-write S0.5).
        m_prefabData = Authoring::DocumentAccess::Adopt(
            SerializeRecursive(source, source->m_prefabFileGuid));
    }
}

Prefab* Prefab::CreateFromGameObject(const Entity* source, std::string_view name)
{
    if (!source)
        return nullptr;

    std::string prefabName = name.empty() ? source->GetHashedName().ToString() + "_Prefab" : std::string(name);
    return new Prefab(prefabName, source);
}

const MetaYml::Node& Prefab::GetPrefabData() const
{
    return Authoring::DocumentAccess::Node(m_prefabData);
}

void Prefab::SetPrefabData(const Authoring::NodeView& view)
{
    m_prefabData = Authoring::DocumentAccess::Adopt(
        Authoring::NodeViewAccess::Node(view).BackendNodeDuringTransition());
}

Entity* Prefab::Instantiate(std::string_view newName) const
{
    // D3-a-3c: 문서의 빈 상태는 타입이 답한다.
    if (m_prefabData.IsEmpty())
        return nullptr;

    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene)
        return nullptr;

    const MetaYml::Node& definition = Authoring::DocumentAccess::Node(m_prefabData);
    if (!definition || !definition.IsSequence() || definition.size() == 0)
        return nullptr;

	// 소환은 정의를 그대로 쓰지 않고 사본 위에서 진행한다 — UpgradeLegacyNavigation이
	// 노드를 고쳐 쓰므로, 사본이 아니면 프리팹 정의 자체가 오염된다.
	MetaYml::Node prefabData = MetaYml::Clone(definition);
	UpgradeLegacyNavigation(prefabData);

    Entity* rootObject = nullptr;
	InstantiateContext context;

	for (std::size_t i = 0; i < prefabData.size(); ++i)
    {
		const MetaYml::Node& gameObjNode = prefabData[i];

        // 첫 번째 GameObject에만 overrideName 적용
        std::string_view nameOverride = (i == 0) ? newName : "";

        Entity* instantiated = InstantiateRecursive(gameObjNode, scene, 0, context, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }
	context.ApplyRootFixups(scene);

    return rootObject;
}

Entity* Prefab::Instantiate(Scene* targetScene, std::string_view newName) const
{
    // D3-a-3c: 문서의 빈 상태는 타입이 답한다.
    if (m_prefabData.IsEmpty())
        return nullptr;

    Scene* scene = targetScene;
    if (!scene)
        return nullptr;

    const MetaYml::Node& definition = Authoring::DocumentAccess::Node(m_prefabData);
    if (!definition || !definition.IsSequence() || definition.size() == 0)
        return nullptr;

	// 소환은 정의를 그대로 쓰지 않고 사본 위에서 진행한다 — UpgradeLegacyNavigation이
	// 노드를 고쳐 쓰므로, 사본이 아니면 프리팹 정의 자체가 오염된다.
	MetaYml::Node prefabData = MetaYml::Clone(definition);
	UpgradeLegacyNavigation(prefabData);

    Entity* rootObject = nullptr;
	InstantiateContext context;

	for (std::size_t i = 0; i < prefabData.size(); ++i)
    {
		const MetaYml::Node& gameObjNode = prefabData[i];

        // 첫 번째 GameObject에만 overrideName 적용
        std::string_view nameOverride = (i == 0) ? newName : "";

        Entity* instantiated = InstantiateRecursive(gameObjNode, scene, 0, context, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }
	context.ApplyRootFixups(scene);

    return rootObject;
}

MetaYml::Node Prefab::SerializeNestedReference(const Entity* obj)
{
    MetaYml::Node node;
    if (!obj)
        return node;

    const Meta::Type* type = Meta::MetaDataRegistry->Find(obj->GetTypeID());
    if (type)
    {
        Entity* nonConst = const_cast<Entity*>(obj);
        node = Meta::Serialize(nonConst, *type);
    }

    // 컴포넌트는 담지 않는다 — 그것이 참조 노드의 요점이다. 담으면 그 순간의
    // 형상이 리터럴로 굳어 중첩 정의 변경이 영영 전달되지 않는다(P4-b가 고치는
    // 결함 그 자체). 로컬 수정은 m_prefabOverrides가 담고, 소환 때
    // PrefabUtility::ApplyRecordedOverrides가 되먹인다.
    node.remove("m_components");

    // children도 담지 않는다. 중첩 프리팹의 하위 구조는 그 프리팹의 정의가
    // 소유한다 — 여기서 굽으면 같은 이유로 굳는다.
    node.remove("children");

    // 계층 인덱스는 소환 시점에 새로 배정된다. 굽힌 값을 남기면 다른 씬에서
    // 되살아날 때 엉뚱한 슬롯을 가리키는 잡음이 된다.
    node[kNestedRefKey] = true;

    return node;
}

MetaYml::Node Prefab::SerializeRecursive(const Entity* obj, FileGuid ownerPrefabGuid)
{
    MetaYml::Node node;
    if (!obj)
        return node;

    const Meta::Type* type = Meta::MetaDataRegistry->Find(obj->GetTypeID());
    if (type)
    {
        Entity* nonConst = const_cast<Entity*>(obj);
        node = Meta::Serialize(nonConst, *type);
    }

	const auto& childIndices = obj->GetChildrenIndices();
    if (!childIndices.empty())
    {
		// 계층 인덱스는 Scene-scoped다. 활성 Scene을 쓰면 PrefabEditor의 비활성
		// 편집 Scene이나 명시 target Scene의 같은 번호 슬롯을 잘못 읽는다.
		Scene* scene = obj->GetScene();
        if (scene)
        {
            MetaYml::Node childrenNode;
			for (auto childIndex : childIndices)
            {
				auto childObj = scene->TryGetEntity(childIndex);
                if (!childObj)
                    continue;

                // ★ P4-b — 중첩 프리팹 인스턴스 루트는 펼치지 않고 참조로 굽는다.
                //
                // 판정 기준은 P4-a가 세운 것과 같다: 자식이 유효한
                // m_prefabFileGuid를 갖고 그것이 지금 굽는 프리팹의 guid와 다르면
                // 그 자식은 **다른 프리팹의 인스턴스 루트**다. 같은 guid를 가진
                // 자손(중첩 루트의 순수 자식들)은 그 중첩 프리팹 정의가 소유하므로
                // 여기서 다시 굽지 않는다 — 애초에 참조 노드가 children을 안 담아
                // 이 재귀에 들어오지도 않는다.
                const FileGuid childGuid = childObj->m_prefabFileGuid;
                const bool isNestedInstanceRoot =
                    (childGuid != nullFileGuid) && (childGuid != ownerPrefabGuid);

                childrenNode.push_back(isNestedInstanceRoot
					? SerializeNestedReference(childObj)
					: SerializeRecursive(childObj, ownerPrefabGuid));
            }
            if (childrenNode)
                node["children"] = childrenNode;
        }
    }

    return node;
}

Entity* Prefab::InstantiateRecursive(const MetaYml::Node& node,
    Scene* scene,
    Entity::Index parent,
	InstantiateContext& context,
    std::string_view overrideName,
    FileGuid inheritedPrefabGuid) const
{
    if (!scene || !node)
        return nullptr;

	const GameObjectType type = EntityAuthoring::InferCreationType(node);
    std::string objName = overrideName.empty() ? node["m_name"].as<std::string>() : std::string(overrideName);
	Entity* obj = scene->LoadEntity(make_guid(), objName, type, parent);
    if (!obj)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<Entity>());
    HashedGuid newInstanceID = obj->GetInstanceID();
	HashingString newHashedName = obj->GetHashedName();
    Entity::Index newIndex = obj->m_index;
	const Entity::Index targetRootIndex = obj->GetRootIndex();
	const Entity::Index sourceIndex = node["m_index"]
		? node["m_index"].as<Entity::Index>() : Entity::INVALID_INDEX;
	const bool hasSourceRootIndex = static_cast<bool>(node["m_rootIndex"]);
	const Entity::Index sourceRootIndex = hasSourceRootIndex
		? node["m_rootIndex"].as<Entity::Index>() : targetRootIndex;
	context.RecordIndex(sourceIndex, newIndex);
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

    // 구파일 승격(레인 2, SceneGraphRedesignPlan §5 예외 4) — 구스키마
    // m_transform 키가 있으면 Transform 컴포넌트에 값을 쓴다(신파일은 무작용).
    LegacyTransformPromotion::PromoteLegacyTransform(obj, node);

	// U7: Navigation은 소스 UI 기준 계층 로컬 경로로 해석한다. 따라서 UI도
	// 다른 엔티티와 똑같이 매 인스턴스마다 새 ID를 받아야 하며, 같은 프리팹을
	// 여러 번 배치해도 각 링크는 자기 계층 안에서만 풀린다.
	obj->m_instanceID = newInstanceID;
	obj->m_name = newHashedName;
    obj->m_index = newIndex;
    obj->SetParentIndex(parent);
    obj->ClearChildren();
	// H3에서 Meta::Deserialize는 계층을 건드리지 않는다. 대상 Scene에서 갓 할당된
	// 안전한 root를 유지하고, 전체 트리가 생기면 context가 source->target remap으로
	// 최종 정정한다.
	obj->SetRootIndex(targetRootIndex);
	if (hasSourceRootIndex)
		context.RecordRoot(obj, newIndex, sourceRootIndex);

    if (!obj->m_tag.ToString().empty())
    {
        TagManager::GetInstance()->AddTagToObject(obj->m_tag.ToString(), obj);
    }

    if (!obj->m_layer.ToString().empty())
    {
        TagManager::GetInstance()->AddObjectToLayer(obj->m_layer.ToString(), obj);
    }

	Entity* parentObj = scene->TryGetEntity(parent);
    if (parentObj && parentObj != obj)
    {
        // 계층 쓰기 정본 API(SceneGraphRedesignPlan 트랙 E2). AttachChildIndex 자체가
        // 중복을 걸러내지만, 여기서는 먼저 find_if로 판정해 기존 경고 로그를 그대로
        // 유지한다(로그를 남기지 않고 조용히 무시하는 쪽으로 동작을 바꾸지 않기 위해).
		const auto& parentChildren = parentObj->GetChildrenIndices();
        if(std::find(parentChildren.begin(), parentChildren.end(), newIndex) == parentChildren.end())
        {
            parentObj->AttachChildIndex(newIndex);
        }
        else
        {
            Debug->LogWarning("Entity with index " + std::to_string(newIndex) + " is already a child of " + std::to_string(parent));
		}

    }

    if (node["m_components"])
    {
        for (const auto componentNode : Authoring::ReadNode{ node }["m_components"])
        {
            try
            {
                ComponentFactorys->LoadComponent(obj, Authoring::NodeViewAccess::Make(componentNode), true);
            }
            catch (const std::exception& e)
            {
                Debug->LogError(e.what());
                continue;
            }
        }
    }

    // 여기에 있던 주석 처리된 블록은 C++ 스크립트가 프리팹 로드 직후 씬 이벤트에
    // 재구독하던 경로였다. 그 계층이 은퇴하면서(9-4) 대응물이 사라졌다 —
    // C# 스크립트는 ScriptComponent::Awake가 인스턴스를 만들고 ClrHost가 틱당
    // 일괄 디스패치하므로, 프리팹 쪽에서 따로 배선할 것이 없다.

    // ★ P4-a: 중첩 프리팹 정체성 보존 (SceneGraphRedesignPlan §4 트랙 P, 확정된
    // 결함 2). 자식 재귀(아래)보다 반드시 먼저 obj 자신의 최종 guid를 정해야
    // 한다 — 그래야 내 자식들이 "부모(나)의 확정 guid"를 물려받을 수 있다.
    //
    // Meta::Deserialize(위)가 이미 node["m_prefabFileGuid"]를 obj->m_prefabFileGuid에
    // 옮겨 놓았다. 그 값이 비어 있지 않다면 이 노드는 저작 시점에 "다른 프리팹의
    // 인스턴스"였다는 뜻이므로(SerializeRecursive가 중첩을 펼칠 때 자식의 살아있던
    // m_prefabFileGuid를 그대로 실어 왔다) 그 정체성을 보존한다 — 예전 코드는 여기서
    // 무조건 바깥 프리팹의 guid로 덮어 중첩 정체성을 파괴했다.
    //
    // 최상위 호출(parent==0, Instantiate()가 처음 넘기는 자리)만 예외다 — 이
    // Instantiate() 호출로 만드는 루트는 항상 "이 프리팹 자신"의 인스턴스이므로
    // 원본 노드에 어떤 값이 있었든 GetFileGuid()로 고정한다(원래 동작 그대로).
    //
    // 그 외의 순수 자식(자기 guid가 없는 노드)은 inheritedPrefabGuid를 물려받는다
    // — 바깥 프리팹의 guid를 무조건 쓰지 않는 이유는, 내가 중첩 프리팹 루트의
    // 자손이면 내가 물려받아야 할 값은 바깥이 아니라 그 중첩 루트의 guid이기
    // 때문이다(ItemSlotPrefab2.prefab의 Box_01처럼 — 부모 ItemSlot이 이미 다른
    // guid를 갖는다).
    const FileGuid ownGuidFromNode = obj->m_prefabFileGuid;
    const FileGuid effectivePrefabGuid = (parent == 0)
        ? GetFileGuid()
        : (ownGuidFromNode != nullFileGuid) ? ownGuidFromNode : inheritedPrefabGuid;

    obj->m_prefabFileGuid = effectivePrefabGuid;
    obj->m_prefab = const_cast<Prefab*>(this);
    // ★ 깊은 복사로 준다. yaml-cpp Node는 참조 의미라, 그냥 대입하면 이 프리팹의
    // **모든 인스턴스가 같은 노드 하나를 스냅샷으로 공유**한다. 그러면
    // PrefabUtility::UpdateInstances가 첫 인스턴스를 처리하며 실행하는
    // obj->m_prefabOriginal = newData 한 줄이 그 공유 노드를 새 데이터로 덮어,
    // 아직 처리되지 않은 나머지 인스턴스의 스냅샷까지 바뀐다.
    //
    // 그 결과는 조용하고 고약하다: 다음 인스턴스의 시딩이 "현재값(옛 값) vs
    // 스냅샷(새 값)"을 비교해 **프리팹이 방금 바꾼 필드를 사용자 오버라이드로
    // 오기록**하고, 그 오버라이드가 갱신을 배제한다. 즉 인스턴스가 N개면
    // 프리팹 갱신이 **첫 하나에만** 먹고 나머지는 옛 값에 영구히 고정된다.
    // 실측: 인스턴스 2개로 재현 — I1 shadowCast=false(적용) · I2 true(고정).
    obj->m_prefabOriginal = Authoring::DocumentAccess::Adopt(MetaYml::Clone(node));

    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            // ★ P4-b — 참조 노드는 그 프리팹의 **현재 정의**로 푼다.
            if (childNode[kNestedRefKey])
            {
				if (!InstantiateNestedReference(childNode, scene, newIndex))
                {
                    // fail-loud. 조용히 건너뛰면 계층에서 서브트리 하나가 통째로
                    // 사라진 채 아무 흔적도 남지 않는다 — 이 저장소가 반복해 겪은
                    // 조용한 유실 양식이다.
                    const std::string childName = childNode["m_name"]
                        ? childNode["m_name"].as<std::string>() : std::string("(이름없음)");
                    Debug->LogError("중첩 프리팹 참조를 풀지 못했다 — 자식 '" + childName
                        + "'이(가) 이 인스턴스에서 누락된다. 참조된 프리팹 자산을 찾을 수 없다");
                }
                continue;
            }

			InstantiateRecursive(childNode, scene, newIndex, context, "", effectivePrefabGuid);
        }
    }

    // 인스턴스 "루트" 판정 — P2의 ReconnectPrefabInstance(SceneManager.cpp)가 이미
    // 쓰는 패턴을 그대로 재사용한다(새로 발명하지 않는다): 부모가 없거나(parent==0)
    // 부모의 guid가 나와 다르면 내가 그 프리팹 인스턴스의 루트다. 예전 코드는
    // `parent == 0` 가드로 최외곽 루트만 등록했다 — 중첩 루트는 parent가 0이 아니라
    // 한 번도 등록되지 않았다(확정된 결함 3).
    const bool isInstanceRoot = (parent == 0) || (effectivePrefabGuid != inheritedPrefabGuid);
    if (isInstanceRoot)
    {
        const Prefab* registerPrefab = this;
        if (effectivePrefabGuid != GetFileGuid())
        {
            // 중첩 루트 — 바깥 프리팹이 아니라 자기 프리팹의 등록부에 잡혀야
            // UpdateInstances(그 중첩 프리팹)가 이 인스턴스를 찾는다. 못 찾으면
            // (자산이 아직 캐시/디스크에 없는 등) this로 폴백한다 — 등록 자체가
            // 안 되는 것보다는 최외곽으로라도 등록되는 편이 안전하다.
            if (Prefab* nested = PrefabUtilitys->LoadPrefabGuid(effectivePrefabGuid))
            {
                obj->m_prefab = nested;
                registerPrefab = nested;
            }
        }
        PrefabUtilitys->RegisterInstance(obj, registerPrefab);
    }

    return obj;
}

Entity* Prefab::InstantiateNestedReference(const MetaYml::Node& node,
    Scene* scene,
    Entity::Index parent) const
{
    if (!scene || !node)
        return nullptr;

    if (!node["m_prefabFileGuid"])
        return nullptr;

    // FileGuid에는 yaml-cpp convert 특수화가 없다(리플렉션 경로로만 오간다).
    // 문자열로 읽어 생성자에 넘긴다 — FromString은 못 읽으면 던진다.
    FileGuid refGuid{};
    try
    {
        refGuid = FileGuid(node["m_prefabFileGuid"].as<std::string>());
    }
    catch (const std::exception&)
    {
        return nullptr;
    }

    if (refGuid == nullFileGuid)
        return nullptr;

    // 자기 자신을 중첩으로 참조하면 무한 재귀다. 굽는 쪽이 guid가 같은 자식을
    // 참조로 만들지 않으므로(SerializeRecursive의 판정) 정상 경로에서는 생기지
    // 않지만, 손으로 편집한 자산이 그런 노드를 담을 수 있다.
    if (refGuid == GetFileGuid())
    {
        Debug->LogError("중첩 프리팹 참조가 자기 자신을 가리킨다 — 무한 재귀를 막으려 건너뛴다");
        return nullptr;
    }

    Prefab* nested = PrefabUtilitys->LoadPrefabGuid(refGuid);
    if (!nested)
        return nullptr;

    const MetaYml::Node& nestedData = nested->GetPrefabData();
    if (!nestedData || !nestedData.IsSequence() || nestedData.size() == 0)
        return nullptr;
	MetaYml::Node upgradedNestedData = MetaYml::Clone(nestedData);
	UpgradeLegacyNavigation(upgradedNestedData);

    // 굽힐 때의 이름을 그대로 쓴다 — 참조 노드가 Entity 필드를 싣고 있으므로
    // 저작자가 붙인 이름이 여기 있다(정의의 이름이 아니라).
    const std::string overrideName = node["m_name"]
        ? node["m_name"].as<std::string>() : std::string();

    // 중첩 프리팹의 **현재 정의**로 만든다. inheritedPrefabGuid에 그 프리팹의
    // guid를 넘기는 이유: 정의 루트 노드 자신의 m_prefabFileGuid는 보통 널이다
    // (굽기 원본이 프리팹 인스턴스가 아니었으므로). 그러면 InstantiateRecursive의
    // effectivePrefabGuid 판정이 inheritedPrefabGuid로 떨어지고, 그 값이 정확히
    // 이 중첩 프리팹의 정체성이 된다(P4-a의 규칙 그대로).
	InstantiateContext nestedContext;
	Entity* child = nested->InstantiateRecursive(upgradedNestedData[0], scene, parent,
		nestedContext, overrideName, nested->GetFileGuid());
    if (!child)
        return nullptr;
	nestedContext.ApplyRootFixups(scene);

    // ★ 등록은 여기서 명시적으로 한다.
    //
    // InstantiateRecursive의 자동 등록은 `parent == 0 || effectiveGuid !=
    // inheritedGuid`로 인스턴스 루트를 판정하는데, 이 호출은 **둘 다 아니다** —
    // parent가 0이 아니고(자식으로 붙는다) effectiveGuid와 inheritedGuid가 같다
    // (바로 위에서 같은 값을 넘겼다). 그래서 자동 판정에 기대면 중첩 루트가
    // 등록부에 들어가지 않고, 그러면 UpdateInstances(그 중첩 프리팹)가 이
    // 인스턴스를 영영 못 찾는다.
    child->m_prefab = nested;
    child->m_prefabFileGuid = refGuid;
    PrefabUtilitys->RegisterInstance(child, nested);

    // 저작 시점의 로컬 수정을 되싣고 되먹인다. 참조 노드는 컴포넌트를 담지
    // 않으므로(정의에서 왔다) 이 두 줄이 없으면 로컬 수정이 매 소환마다 사라진다.
    child->m_prefabOverrides.clear();
    if (node["m_prefabOverrides"] && node["m_prefabOverrides"].IsSequence())
    {
        // 항목 하나씩 리플렉션으로 읽는다 — 손으로 필드를 꺼내면 PrefabOverride의
        // 스키마가 바뀔 때 이 자리만 조용히 낡는다(m_componentSlot이 P4-c에서
        // 늘어난 것이 실례다).
        for (const auto& ovNode : node["m_prefabOverrides"])
        {
            try
            {
                PrefabOverride ov;
                Meta::Deserialize(&ov, ovNode);
                child->m_prefabOverrides.push_back(std::move(ov));
            }
            catch (const std::exception& e)
            {
                Debug->LogError("중첩 참조의 오버라이드 항목을 읽지 못했다: " + std::string(e.what()));
            }
        }
    }
    PrefabUtility::ApplyRecordedOverrides(*child);

    // 되먹임은 리플렉션으로 값을 직접 써 넣는다 — Transform 세터를 거치지
    // 않으므로 dirty가 서지 않는다(UpdateInstances가 같은 이유로 하는 일).
    child->Transform_().SetDirty();

    return child;
}
