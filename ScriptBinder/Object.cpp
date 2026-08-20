#include "Object.h"
#include "GameObject.h"
#include "ComponentFactory.h"
#include "PrefabUtility.h"
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
#include "TagManager.h"

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
    auto* go = dynamic_cast<Entity*>(objPtr);
    if (!go)
    {
        return;
    }

    // Already marked: nothing to do
    if (go->m_dontDestroyOnLoad) return;

    // Promote to root
    while (Entity::IsValidIndex(go->m_parentIndex))
    {
        auto sc = go->GetScene();
        auto parent = sc ? sc->GetGameObject(go->m_parentIndex) : nullptr;
        if (!parent) break;
        // 다음 부모가 씬 루트(0)거나, 부모의 부모가 INVALID면 여기서 멈춤
        if (parent->m_index == 0 || !Entity::IsValidIndex(parent->m_parentIndex))
            break;
        go = parent.get();
    }

    // Collect subtree & mark DDOL
    std::vector<std::shared_ptr<Object>> collected;
    Scene* originScene = go->GetScene();

    auto markDdol = [&](auto&& self, Entity* node) -> void {
        if (!node) return;
        if (node->m_index == 0) return;
        node->m_dontDestroyOnLoad = true;
        collected.push_back(node->shared_from_this());
        if (!originScene) return;
        for (auto childIdx : node->m_childrenIndices)
        {
            if (Entity::IsValidIndex(childIdx))
            {
                auto child = originScene->GetGameObject(childIdx);
                if (child) self(self, child.get());
            }
        }
    };
    markDdol(markDdol, go);

    // Ensure root is detached from any parent (keep world)
    go->SetParentIndex(Entity::INVALID_INDEX);
    go->SetRootIndex(Entity::INVALID_INDEX);

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

	// E7-c: Entity는 저장 타입 필드 없이도 공간 컴포넌트 조합을 정확히 복제해야
	// 한다. meta->create()는 기본 Entity(Transform)를 만들기 때문에 UI 원본에
	// 사용하면 RectTransform을 추가한 뒤 둘을 함께 갖게 된다. Entity만 Scene의
	// 정식 생성 경로를 먼저 타고, 나머지 Object는 아래 기존 팩토리를 유지한다.
	if (auto* originalEntity = dynamic_cast<const Entity*>(original))
	{
		Scene* scene = const_cast<Entity*>(originalEntity)->GetScene();
		if (!scene) scene = SceneManagers->GetActiveScene();
		if (!scene) return nullptr;

		MetaYml::Node originalNode = Meta::Serialize(
			const_cast<Entity*>(originalEntity), *meta);
		const std::string cloneName = !newName.empty()
			? std::string(newName)
			: originalEntity->m_name.ToString() + "_Clone";

		auto ownedClone = scene->CreateGameObject(
			cloneName, Entity::InferCreationType(originalNode));
		Entity* clone = ownedClone.get();
		if (!clone) return nullptr;

		const HashedGuid newInstanceID = clone->m_instanceID;
		const HashingString newHashedName = clone->m_name;
		const Entity::Index newIndex = clone->m_index;
		const Entity::Index newParentIndex = clone->m_parentIndex;

		Meta::Deserialize(clone, originalNode);
		clone->m_instanceID = newInstanceID;
		clone->m_name = newHashedName;
		clone->m_index = newIndex;
		clone->SetParentIndex(newParentIndex);
		clone->ClearChildren();

		if (!clone->m_tag.ToString().empty())
			TagManager::GetInstance()->AddTagToObject(clone->m_tag.ToString(), clone);
		if (!clone->m_layer.ToString().empty())
			TagManager::GetInstance()->AddObjectToLayer(clone->m_layer.ToString(), clone);

		if (originalNode["m_components"])
		{
			for (const auto& componentNode : originalNode["m_components"])
			{
				try { ComponentFactorys->LoadComponent(clone, componentNode, true); }
				catch (const std::exception& e) { Debug->LogError(e.what()); }
			}
		}

		if (nullptr != PrefabUtilitys && clone->m_prefabFileGuid != FileGuid{})
		{
			if (Prefab* prefab = PrefabUtilitys->LoadPrefabGuid(clone->m_prefabFileGuid))
				PrefabUtilitys->RegisterInstance(clone, prefab);
		}

		for (Entity::Index childIndex : originalEntity->m_childrenIndices)
		{
			auto child = scene->TryGetGameObject(childIndex);
			if (!child) continue;

			auto* childClone = dynamic_cast<Entity*>(
				Instantiate(child.get(), child->m_name.ToString()));
			if (!childClone) continue;

			scene->GetRootObject()->DetachChildIndex(childClone->m_index);
			childClone->SetParentIndex(clone->m_index);
			childClone->SetRootIndex(clone->m_rootIndex);
			clone->AttachChildIndex(childClone->m_index);
		}

		return clone;
	}

	// 새 인스턴스 생성
    // CT11: 팩토리 접합 — meta(Type*)가 생성 함수를 직접 든다(조회 0회).
    Object* cloneObj = meta->create ? static_cast<Object*>(meta->create()) : nullptr;
    if (!cloneObj)
        return nullptr;

	cloneObj->m_typeID = original->m_typeID;
    // 이름 설정
    if (!newName.empty())
        cloneObj->m_name = newName;
    else
        cloneObj->m_name = original->m_name.ToString() + "_Clone";

    return cloneObj;

}
