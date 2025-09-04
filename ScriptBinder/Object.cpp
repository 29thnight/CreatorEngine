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
    if (!go) return;

    // Already marked: nothing to do
    if (go->m_dontDestroyOnLoad) return;

    // Promote to root
    while (GameObject::IsValidIndex(go->m_parentIndex))
    {
        auto sc = go->GetScene();
        if (!sc) break;
        auto parent = sc->GetGameObject(go->m_parentIndex);
        if (parent) go = parent.get();
        else break;
    }

    // Collect subtree & mark DDOL
    std::vector<std::shared_ptr<Object>> collected;
    Scene* originScene = go->GetScene();

    auto markDdol = [&](auto&& self, GameObject* node) -> void {
        if (!node) return;
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

    // Detach fully from origin scene containers
    if (originScene) { originScene->DetachGameObjectHierarchy(go); }

    // (E) 원 씬의 각종 컬렉션/이벤트에서 DDOL 서브트리를 완전히 분리
    if (originScene)
    {
        for (auto& o : collected)
        {
            auto g = std::dynamic_pointer_cast<GameObject>(o);
            if (!g) continue;

            // Scene 컬렉션에서 UnCollect
            for (auto& comp : g->m_components)
            {
                if (!comp) continue;
                if (auto* c = dynamic_cast<LightComponent*>(comp.get()))              originScene->UnCollectLightComponent(c);
                else if (auto* c = dynamic_cast<MeshRenderer*>(comp.get()))           originScene->UnCollectMeshRenderer(c);
                else if (auto* c = dynamic_cast<TerrainComponent*>(comp.get()))       originScene->UnCollectTerrainComponent(c);
                else if (auto* c = dynamic_cast<FoliageComponent*>(comp.get()))       originScene->UnCollectFoliageComponent(c);
                else if (auto* c = dynamic_cast<RigidBodyComponent*>(comp.get()))     originScene->UnCollectRigidBodyComponent(c);
                else if (auto* c = dynamic_cast<BoxColliderComponent*>(comp.get()))   originScene->UnCollectColliderComponent(c);
                else if (auto* c = dynamic_cast<SphereColliderComponent*>(comp.get()))originScene->UnCollectColliderComponent(c);
                else if (auto* c = dynamic_cast<CapsuleColliderComponent*>(comp.get()))originScene->UnCollectColliderComponent(c);
                else if (auto* c = dynamic_cast<MeshColliderComponent*>(comp.get()))  originScene->UnCollectColliderComponent(c);
                else if (auto* c = dynamic_cast<CharacterControllerComponent*>(comp.get())) originScene->UnCollectColliderComponent(c);
                else if (auto* c = dynamic_cast<TerrainColliderComponent*>(comp.get()))    originScene->UnCollectColliderComponent(c);
                // (참고) IRegistableEvent의 Unregister 계열 API가 있다면 여기서 호출하세요.
            }
            // 원 씬 포인터 끊기
            g->m_ownerScene = nullptr;
        }
    }

    // Ensure root is detached from any parent (keep world)
    go->m_parentIndex = GameObject::INVALID_INDEX;
    go->m_rootIndex = GameObject::INVALID_INDEX;
    go->m_transform.SetParentID(-1);

    // Register to global DDOL bucket
    for (auto& o : collected)
    {
        SceneManagers->AddDontDestroyOnLoad(o);
    }

    // (F) 즉시 재바인딩 제거: 씬 로드시 Rebind 처리
}

Object* Object::Instantiate(const Object* original, std::string_view newName)
{
    if (!original)
        return nullptr;

    const Meta::Type* meta = Meta::MetaDataRegistry->Find(original->GetTypeID());
    if (!meta)
        return nullptr;

    // 새 인스턴스 생성
    Object* cloneObj = Meta::MetaFactoryRegistry->Create<Object>(meta->name);
    if (!cloneObj)
        return nullptr;

	cloneObj->m_instanceID = make_guid();
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
		auto originalNode = Meta::Serialize(originalGameObject, *meta);

		Meta::Deserialize(cloneGameObject, originalNode);
        cloneGameObject->m_childrenIndices.clear();

        Scene* scene = SceneManagers->GetActiveScene();
        if (scene)
            scene->AddGameObject(std::shared_ptr<GameObject>(cloneGameObject));

        if(0 < originalGameObject->m_childrenIndices.size())
        {
            //cloneGameObject->m_childrenIndices.clear();
            for (auto index : originalGameObject->m_childrenIndices)
            {
                if (!scene) 
					continue;

                auto& rootChildren = scene->m_SceneObjects[0]->m_childrenIndices;
                auto childGameObject = scene->GetGameObject(index);
				if (childGameObject)
				{
					auto childClone = Instantiate(childGameObject.get(), childGameObject->m_name.ToString());
					GameObject* childCloneGameObject = dynamic_cast<GameObject*>(childClone);
					childCloneGameObject->m_parentIndex = cloneGameObject->m_index;
					childCloneGameObject->m_transform.SetParentID(childCloneGameObject->m_index);
                    std::erase(rootChildren, childCloneGameObject->m_index);
                    childCloneGameObject->m_rootIndex = cloneGameObject->m_rootIndex;
                    cloneGameObject->m_childrenIndices.push_back(childCloneGameObject->m_index);
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
