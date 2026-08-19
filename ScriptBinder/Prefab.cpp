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
        m_prefabData = SerializeRecursive(source);
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

MetaYml::Node Prefab::SerializeRecursive(const Entity* obj)
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
                if (childObj)
                    childrenNode.push_back(SerializeRecursive(childObj.get()));
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
    obj->m_prefabOriginal = node;

    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            auto childObj = InstantiateRecursive(childNode, scene, obj->m_index, "", effectivePrefabGuid);
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