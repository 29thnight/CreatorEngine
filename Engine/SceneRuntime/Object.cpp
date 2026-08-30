#include "Object.h"
#include "AuthoringNodeViewAccess.h" // D3-a-5
#include "EntityAuthoringRead.h" // D3-a-2: 저작 읽기 어댑터
#include "Entity.h"
#include "ComponentFactory.h"
#include "PrefabUtility.h"
#include "SceneManager.h"
#include <algorithm>
#include <unordered_map>
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
	Entity::Index parentIndex = go->GetParentIndex();
	while (Entity::IsValidIndex(parentIndex))
    {
		Entity* parent = go->OwnerSceneFindIndex(parentIndex);
        if (!parent) break;
        // 다음 부모가 씬 루트(0)거나, 부모의 부모가 INVALID면 여기서 멈춤
		parentIndex = parent->GetParentIndex();
		if (parent->m_index == 0 || !Entity::IsValidIndex(parentIndex))
            break;
		go = parent;
    }

    // Collect subtree & mark DDOL
	std::vector<Object*> collected;
    Scene* originScene = go->GetScene();

    auto markDdol = [&](auto&& self, Entity* node) -> void {
        if (!node) return;
        if (node->m_index == 0) return;
        node->m_dontDestroyOnLoad = true;
		collected.push_back(node);
        if (!originScene) return;
		for (const Entity::Index childIdx : node->GetChildrenIndices())
        {
            if (Entity::IsValidIndex(childIdx))
            {
                auto child = originScene->GetEntity(childIdx);
				if (child) self(self, child);
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

		// H3: 계층 필드가 Entity에서 사라졌으므로 Meta::Deserialize가 root 참조를
		// 암묵 복사하지 않는다. 서브트리를 모두 만든 뒤 source slot→clone slot로
		// root 참조를 고치는 컨텍스트를 이 복제 한 번에 공유한다.
		struct RootFixup
		{
			Entity* clone{ nullptr };
			Entity::Index sourceRoot{ Entity::INVALID_INDEX };
		};
		std::unordered_map<Entity::Index, Entity::Index> sourceToClone;
		std::vector<RootFixup> rootFixups;

		auto cloneRecursive = [&](auto&& self, const Entity* source,
			std::string_view requestedName) -> Entity*
		{
			if (!source) return nullptr;
			const Meta::Type* sourceMeta = Meta::MetaDataRegistry->Find(source->GetTypeID());
			if (!sourceMeta) return nullptr;

			MetaYml::Node sourceNode = Meta::Serialize(
				const_cast<Entity*>(source), *sourceMeta);
			const std::string cloneName = !requestedName.empty()
				? std::string(requestedName)
				: source->m_name.ToString() + "_Clone";

			Entity* clone = scene->CreateEntity(
				cloneName, EntityAuthoring::InferCreationType(sourceNode));
			if (!clone) return nullptr;

			const HashedGuid newInstanceID = clone->m_instanceID;
			const HashingString newHashedName = clone->m_name;
			const Entity::Index newIndex = clone->m_index;
			const Entity::Index newParentIndex = clone->GetParentIndex();
			const Entity::Index sourceRootIndex = source->GetRootIndex();

			Meta::Deserialize(clone, sourceNode);
			clone->m_instanceID = newInstanceID;
			clone->m_name = newHashedName;
			clone->m_index = newIndex;
			clone->SetParentIndex(newParentIndex);
			clone->ClearChildren();
			sourceToClone[source->m_index] = newIndex;
			rootFixups.push_back({ clone, sourceRootIndex });

			if (!clone->m_tag.ToString().empty())
				TagManager::GetInstance()->AddTagToObject(clone->m_tag.ToString(), clone);
			if (!clone->m_layer.ToString().empty())
				TagManager::GetInstance()->AddObjectToLayer(clone->m_layer.ToString(), clone);

			if (sourceNode["m_components"])
			{
				for (const auto& componentNode : sourceNode["m_components"])
				{
					try { ComponentFactorys->LoadComponent(clone, Authoring::NodeViewAccess::Make(componentNode), true); }
					catch (const std::exception& e) { Debug->LogError(e.what()); }
				}
			}

			if (nullptr != PrefabUtilitys && clone->m_prefabFileGuid != FileGuid{})
			{
				if (Prefab* prefab = PrefabUtilitys->LoadPrefabGuid(clone->m_prefabFileGuid))
					PrefabUtilitys->RegisterInstance(clone, prefab);
			}

			// 재귀 생성이 Store의 바깥 children 배열을 재할당할 수 있으므로 복사본을 돈다.
			const std::vector<Entity::Index> sourceChildren = source->GetChildrenIndices();
			for (Entity::Index childIndex : sourceChildren)
			{
				Entity* child = scene->TryGetEntity(childIndex);
				if (!child) continue;

				Entity* childClone = self(self, child, child->m_name.ToString());
				if (!childClone) continue;

				if (Entity* sceneRoot = scene->GetRootEntity())
					sceneRoot->DetachChildIndex(childClone->m_index);
				childClone->SetParentIndex(clone->m_index);
				clone->AttachChildIndex(childClone->m_index);
			}
			return clone;
		};

		Entity* rootClone = cloneRecursive(cloneRecursive, originalEntity, newName);
		for (const RootFixup& fixup : rootFixups)
		{
			if (!fixup.clone) continue;
			if (!Entity::IsValidIndex(fixup.sourceRoot))
			{
				fixup.clone->SetRootIndex(Entity::INVALID_INDEX);
				continue;
			}

			if (const auto found = sourceToClone.find(fixup.sourceRoot);
				found != sourceToClone.end())
			{
				fixup.clone->SetRootIndex(found->second);
			}
			else
			{
				// 복제 범위 밖의 same-scene root 참조는 외부 참조로서 그대로 보존한다.
				fixup.clone->SetRootIndex(fixup.sourceRoot);
			}
		}
		return rootClone;
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
