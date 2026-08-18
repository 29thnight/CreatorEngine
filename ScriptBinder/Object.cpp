#include "Object.h"
#include "GameObject.h"
#include "ComponentFactory.h"
#include "SceneManager.h"
#include <algorithm>
// (E) 원 씬 컬렉션/이벤트에서 안전하게 분리하기 위해 컴포넌트 타입 참조 추가
#include "Scene.h"
#include "LightComponent.h"
#include "MeshRenderer.h"
#include "Terrain.h"
#include "FoliageComponent.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "MeshCollider.h"
#include "CharacterControllerComponent.h"
#include "TerrainCollider.h"

void Object::Destroy()
{
    if (m_destroyMark)
    {
        return;
    }
    m_destroyMark = true;
    TypeTrait::GUIDCreator::EraseGUID(m_instanceID);
}

void Object::Destroy(Object* objPtr)
{
    if (objPtr == nullptr) return;
    objPtr->Destroy();
}

void Object::SetDontDestroyOnLoad(Object* objPtr)
{
    auto* go = dynamic_cast<GameObject*>(objPtr);
    if (!go)
    {
        return;
    }

    // Already marked: nothing to do
    if (go->m_dontDestroyOnLoad) return;

    // Promote to root
    while (GameObject::IsValidIndex(go->m_parentIndex))
    {
        auto sc = go->GetScene();
        auto parent = sc ? sc->GetGameObject(go->m_parentIndex) : nullptr;
        if (!parent) break;
        // 다음 부모가 씬 루트(0)거나, 부모의 부모가 INVALID면 여기서 멈춤
        if (parent->m_index == 0 || !GameObject::IsValidIndex(parent->m_parentIndex))
            break;
        go = parent.get();
    }

    // Collect subtree & mark DDOL
    std::vector<std::shared_ptr<Object>> collected;
    Scene* originScene = go->GetScene();

    auto markDdol = [&](auto&& self, GameObject* node) -> void {
        if (!node) return;
        if (node->m_index == 0) return;
        node->m_dontDestroyOnLoad = true;
        collected.push_back(node->shared_from_this());
        if (!originScene) return;
        for (auto childIdx : node->m_childrenIndices)
        {
            if (GameObject::IsValidIndex(childIdx))
            {
                auto child = originScene->GetGameObject(childIdx);
                if (child) self(self, child.get());
            }
        }
    };
    markDdol(markDdol, go);

    // Ensure root is detached from any parent (keep world)
    go->SetParentIndex(GameObject::INVALID_INDEX);
    go->SetRootIndex(GameObject::INVALID_INDEX);

    // Register to global DDOL bucket
    for (auto& o : collected)
    {
        SceneManagers->AddDontDestroyOnLoad(o);
    }
}

Object* Object::Instantiate(const Object* original, std::string_view newName)
{
    if (!original)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(original->GetTypeID());
    if (!meta)
        return nullptr;

    // 새 인스턴스 생성
    // CT11: 팩토리 접합 — meta(Type*)가 생성 함수를 직접 든다(조회 0회).
    Object* cloneObj = meta->create ? static_cast<Object*>(meta->create()) : nullptr;
    if (!cloneObj)
        return nullptr;

	// 생성자가 발급한 instanceID. 아래 Deserialize가 m_instanceID도 Property라
	// 원본 값으로 덮어써서, 재발급 직전엔 이 값을 필드에서 더 읽을 수 없다 —
	// 미리 붙잡아 두지 않으면 g_guids에 고아 항목으로 영영 남는다.
	const HashedGuid clonedConstructedID = cloneObj->m_instanceID;

	cloneObj->m_typeID = original->m_typeID;
    // 이름 설정
    if (!newName.empty())
        cloneObj->m_name = newName;
    else
        cloneObj->m_name = original->m_name.ToString() + "_Clone";

    GameObject* cloneGameObject = dynamic_cast<GameObject*>(cloneObj);
    Object* originalObj = const_cast<Object*>(original);
    GameObject* originalGameObject = dynamic_cast<GameObject*>(originalObj);

    // GameObject라면 Scene에 등록하고 컴포넌트 복제
    if (cloneGameObject && originalGameObject)
    {
		// 레인 2 판정(SceneGraphRedesignPlan §5 예외 4, 구파일 승격) — 이 originalNode는
		// 파일이 아니라 지금 살아있는 originalGameObject를 Meta::Serialize로 그 자리에서
		// 재직렬화한 인메모리 노드다. GameObject::reflect()가 이미 m_transform 필드를
		// 갖지 않는 스키마로 동작 중이므로(레인 1) 이 노드는 절대 "m_transform" 키를
		// 낼 수 없고 — 대신 Transform 컴포넌트가 m_components 안에 정상적으로 실린다.
		// 즉 이 클론 경로는 구파일 승격(LegacyTransformPromotion::PromoteLegacyTransform,
		// SceneManager.cpp) 대상이 아니다 — 원본이 이미 그 경로(SceneManager.cpp 3곳·
		// Prefab.cpp 1곳 중 하나)로 로드되며 승격을 마쳤기 때문이다. 그래서 여기서는
		// 일부러 승격 호출을 넣지 않았다.
		auto originalNode = Meta::Serialize(originalGameObject, *meta);

		Meta::Deserialize(cloneGameObject, originalNode);
        TypeTrait::GUIDCreator::EraseGUID(clonedConstructedID);
        cloneObj->m_instanceID = make_guid();
        cloneGameObject->ClearChildren();

        Scene* scene = originalGameObject->GetScene();
        if (!scene)
            scene = SceneManagers->GetActiveScene();
        if (scene)
            scene->AddGameObject(std::shared_ptr<GameObject>(cloneGameObject));

        if(0 < originalGameObject->m_childrenIndices.size())
        {
            //cloneGameObject->m_childrenIndices.clear();
            for (auto index : originalGameObject->m_childrenIndices)
            {
                if (!scene)
					continue;

                auto childGameObject = scene->GetGameObject(index);
				if (childGameObject)
				{
					auto childClone = Instantiate(childGameObject.get(), childGameObject->m_name.ToString());
					GameObject* childCloneGameObject = dynamic_cast<GameObject*>(childClone);
					if (!childCloneGameObject) continue;

					childCloneGameObject->SetParentIndex(cloneGameObject->m_index);
                    scene->m_SceneObjects[0]->DetachChildIndex(childCloneGameObject->m_index);
                    childCloneGameObject->SetRootIndex(cloneGameObject->m_rootIndex);
                    cloneGameObject->AttachChildIndex(childCloneGameObject->m_index);
				}
            }
        }

        if (originalNode["m_components"])
        {
			for (const auto& componentNode : originalNode["m_components"])
			{
                try
                {
                    ComponentFactorys->LoadComponent(cloneGameObject, componentNode, true);
                }
                catch (const std::exception& e)
                {
                    Debug->LogError(e.what());
                    continue;
                }
			}
        }
    }

    return cloneObj;

}
