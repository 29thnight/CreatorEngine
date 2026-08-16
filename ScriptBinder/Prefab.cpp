#include "Prefab.h"
#include "GameObject.h"
#include "PrefabUtility.h"
#include "TagManager.h"
#include "Scene.h"

Prefab::Prefab(std::string_view name, const GameObject* source)
    : Object(name)
{
    m_typeID = TypeTrait::GUIDCreator::GetTypeID<Prefab>();
    if (source)
    {
        m_prefabData = SerializeRecursive(source);
    }
}

Prefab* Prefab::CreateFromGameObject(const GameObject* source, std::string_view name)
{
    if (!source)
        return nullptr;

    std::string prefabName = name.empty() ? source->GetHashedName().ToString() + "_Prefab" : std::string(name);
    return new Prefab(prefabName, source);
}

GameObject* Prefab::Instantiate(std::string_view newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene)
        return nullptr;

    if (!m_prefabData || !m_prefabData.IsSequence() || m_prefabData.size() == 0)
        return nullptr;

    GameObject* rootObject = nullptr;

    for (std::size_t i = 0; i < m_prefabData.size(); ++i)
    {
        const MetaYml::Node& gameObjNode = m_prefabData[i];

        // ù ��° GameObject���� overrideName ����
        std::string_view nameOverride = (i == 0) ? newName : "";

        GameObject* instantiated = InstantiateRecursive(gameObjNode, scene, 0, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }

    return rootObject;
}

GameObject* Prefab::Instantiate(Scene* targetScene, std::string_view newName) const
{
    if (!m_prefabData)
        return nullptr;

    Scene* scene = targetScene;
    if (!scene)
        return nullptr;

    if (!m_prefabData || !m_prefabData.IsSequence() || m_prefabData.size() == 0)
        return nullptr;

    GameObject* rootObject = nullptr;

    for (std::size_t i = 0; i < m_prefabData.size(); ++i)
    {
        const MetaYml::Node& gameObjNode = m_prefabData[i];

        // ù ��° GameObject���� overrideName ����
        std::string_view nameOverride = (i == 0) ? newName : "";

        GameObject* instantiated = InstantiateRecursive(gameObjNode, scene, 0, nameOverride);

        if (i == 0)
            rootObject = instantiated;
    }

    return rootObject;
}

MetaYml::Node Prefab::SerializeRecursive(const GameObject* obj)
{
    MetaYml::Node node;
    if (!obj)
        return node;

    const Meta::Type* type = Meta::MetaDataRegistry->Find(obj->GetTypeID());
    if (type)
    {
        GameObject* nonConst = const_cast<GameObject*>(obj);
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

GameObject* Prefab::InstantiateRecursive(const MetaYml::Node& node,
    Scene* scene,
    GameObject::Index parent,
    std::string_view overrideName) const
{
    if (!scene || !node)
        return nullptr;

    GameObjectType type = static_cast<GameObjectType>(node["m_gameObjectType"].as<int>());
    std::string objName = overrideName.empty() ? node["m_name"].as<std::string>() : std::string(overrideName);
    auto objPtr = scene->LoadGameObject(make_guid(), objName, type, parent);
    GameObject* obj = objPtr.get();
    if (!obj)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(TypeTrait::GUIDCreator::GetTypeID<GameObject>());
    HashedGuid newInstanceID = obj->GetInstanceID();
	HashingString newHashedName = obj->GetHashedName();
    GameObject::Index newIndex = obj->m_index;
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
            [&](GameObject::Index index) { return index == newIndex; }) == parentObj->m_childrenIndices.end())
        {
            parentObj->AttachChildIndex(newIndex);
        }
        else
        {
            Debug->LogWarning("GameObject with index " + std::to_string(newIndex) + " is already a child of " + std::to_string(parent));
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

    if (node["children"])
    {
        for (const auto& childNode : node["children"])
        {
            auto childObj = InstantiateRecursive(childNode, scene, obj->m_index);
        }
    }

    obj->m_prefabFileGuid = m_fileGuid;
    obj->m_prefab = const_cast<Prefab*>(this);
    obj->m_prefabFileGuid = GetFileGuid();
    obj->m_prefabOriginal = node;
    if (parent == 0)
        PrefabUtilitys->RegisterInstance(obj, this);

    return obj;
}