#include "Prefab.h"
#include "GameObject.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "Scene.h"

// 구파일 승격 공유 헬퍼(레인 2, SceneGraphRedesignPlan §5 예외 4) — 정의는
// SceneManager.cpp에 있다(그쪽 파일 상단 주석 참고). 헤더를 새로 두지 않기
// 위해(배정 파일 밖 편집 금지) 여기서 직접 forward-declare 한다.
namespace LegacyTransformPromotion
{
    void PromoteLegacyTransform(Entity* obj, const MetaYml::Node& node);
}

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
        m_prefabData = SerializeRecursive(source, source->m_prefabFileGuid);
    }
}

Prefab* Prefab::CreateFromGameObject(const Entity* source, std::string_view name)
{
    if (!source)
        return nullptr;

    std::string prefabName = name.empty() ? source->GetHashedName().ToString() + "_Prefab" : std::string(name);
    return new Prefab(prefabName, source);
}

Entity* Prefab::Instantiate(std::string_view newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene)
        return nullptr;

    if (!m_prefabData || !m_prefabData.IsSequence() || m_prefabData.size() == 0)
        return nullptr;

    Entity* rootObject = nullptr;

    for (std::size_t i = 0; i < m_prefabData.size(); ++i)
    {
        const MetaYml::Node& gameObjNode = m_prefabData[i];

        // ù ��° GameObject���� overrideName ����
        std::string_view nameOverride = (i == 0) ? newName : "";

        Entity* instantiated = InstantiateRecursive(gameObjNode, scene, 0, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }

    return rootObject;
}

Entity* Prefab::Instantiate(Scene* targetScene, std::string_view newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = targetScene;
    if (!scene)
        return nullptr;

    if (!m_prefabData || !m_prefabData.IsSequence() || m_prefabData.size() == 0)
        return nullptr;

    Entity* rootObject = nullptr;

    for (std::size_t i = 0; i < m_prefabData.size(); ++i)
    {
        const MetaYml::Node& gameObjNode = m_prefabData[i];

        // ù ��° GameObject���� overrideName ����
        std::string_view nameOverride = (i == 0) ? newName : "";

        Entity* instantiated = InstantiateRecursive(gameObjNode, scene, 0, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }

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

    if (!obj->m_childrenIndices.empty())
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (scene)
        {
            MetaYml::Node childrenNode;
            for (auto childIndex : obj->m_childrenIndices)
            {
                auto childObj = scene->GetGameObject(childIndex);
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
                    ? SerializeNestedReference(childObj.get())
                    : SerializeRecursive(childObj.get(), ownerPrefabGuid));
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
    std::string_view overrideName,
    FileGuid inheritedPrefabGuid) const
{
    if (!scene || !node)
        return nullptr;

    GameObjectType type = static_cast<GameObjectType>(node["m_gameObjectType"].as<int>());
    std::string objName = overrideName.empty() ? node["m_name"].as<std::string>() : std::string(overrideName);
    auto objPtr = scene->LoadGameObject(make_guid(), objName, type, parent);
    Entity* obj = objPtr.get();
    if (!obj)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<Entity>());
    HashedGuid newInstanceID = obj->GetInstanceID();
	HashingString newHashedName = obj->GetHashedName();
    Entity::Index newIndex = obj->m_index;
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

    // N-14 판정(SceneGraphRedesignPlan P2 조사, UISystemRedesignPlan C3와 같은
    // 결정에 묶임) — 이 스킵은 지금 지우면 안 된다. UI 컴포넌트의
    // Navigation.navObject(UIComponent.cpp)는 오브젝트가 아니라 instanceID를
    // 참조로 저장하고, 그 값은 이 프리팹의 노드 데이터(node)에 저작 시점
    // instanceID로 이미 박혀 있다. UI 타입만 재발급을 건너뛰기 때문에 인스턴스화
    // 직후에도 형제 UI끼리의 참조가 그 값 그대로(OwnerSceneFindInstanceID) 풀린다.
    // 코드베이스 어디에도 Navigation.navObject를 새 instanceID로 다시 써주는
    // 경로가 없다(ComponentFactory.cpp의 navigations 역직렬화는 원본 값을 그대로
    // 옮겨 담을 뿐이다) — 그래서 여기서 UI도 새 instanceID를 받게 하면 "같은
    // 프리팹을 두 번 배치했을 때만" 깨지는 지금의 충돌(UISystemRedesignPlan C3가
    // 적은 버그)이 아니라 "그 프리팹을 인스턴스화할 때마다 매번" 전부 깨진다 —
    // 지금 제거하면 고치는 버그보다 넓게 새로 낸다. UISystemRedesignPlan U7이
    // 계획한 "Navigation instanceID→프리팹-로컬 인덱스 재계산" 같은 대체 배선이
    // 서기 전에는 이 스킵을 걷어낼 수 없다. 제거 자체는 그 계획(C3)이 맡는다 —
    // 이 트랙(P2)은 판정만 하고 현상을 유지한다.
    if (type != GameObjectType::UI)
    {
        obj->m_instanceID = newInstanceID;
    }
	obj->m_name = newHashedName;
    obj->m_index = newIndex;
    obj->SetParentIndex(parent);
    obj->ClearChildren();

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
        // 계층 쓰기 정본 API(SceneGraphRedesignPlan 트랙 E2). AttachChildIndex 자체가
        // 중복을 걸러내지만, 여기서는 먼저 find_if로 판정해 기존 경고 로그를 그대로
        // 유지한다(로그를 남기지 않고 조용히 무시하는 쪽으로 동작을 바꾸지 않기 위해).
        if(std::find_if(parentObj->m_childrenIndices.begin(), parentObj->m_childrenIndices.end(),
            [&](Entity::Index index) { return index == newIndex; }) == parentObj->m_childrenIndices.end())
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
    obj->m_prefabOriginal = MetaYml::Clone(node);

    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            // ★ P4-b — 참조 노드는 그 프리팹의 **현재 정의**로 푼다.
            if (childNode[kNestedRefKey])
            {
                if (!InstantiateNestedReference(childNode, scene, obj->m_index))
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

            InstantiateRecursive(childNode, scene, obj->m_index, "", effectivePrefabGuid);
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

    // 굽힐 때의 이름을 그대로 쓴다 — 참조 노드가 Entity 필드를 싣고 있으므로
    // 저작자가 붙인 이름이 여기 있다(정의의 이름이 아니라).
    const std::string overrideName = node["m_name"]
        ? node["m_name"].as<std::string>() : std::string();

    // 중첩 프리팹의 **현재 정의**로 만든다. inheritedPrefabGuid에 그 프리팹의
    // guid를 넘기는 이유: 정의 루트 노드 자신의 m_prefabFileGuid는 보통 널이다
    // (굽기 원본이 프리팹 인스턴스가 아니었으므로). 그러면 InstantiateRecursive의
    // effectivePrefabGuid 판정이 inheritedPrefabGuid로 떨어지고, 그 값이 정확히
    // 이 중첩 프리팹의 정체성이 된다(P4-a의 규칙 그대로).
    Entity* child = nested->InstantiateRecursive(nestedData[0], scene, parent,
        overrideName, nested->GetFileGuid());
    if (!child)
        return nullptr;

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