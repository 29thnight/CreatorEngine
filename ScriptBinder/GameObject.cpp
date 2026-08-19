#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "TagManager.h"
#include "RectTransformComponent.h"
#include "PrefabUtility.h"
#ifndef DYNAMICCPP_EXPORTS
#include "ScriptObjectRegistry.h"
#endif

Entity::Entity() :
	Object("Entity"),
	m_gameObjectType(GameObjectType::Empty),
	m_index(0),
	m_parentIndex(-1)
{
	m_ownerScene = SceneManagers->GetActiveScene();
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
	// S1-b: 값 멤버 m_transform 소멸 — Transform도 다른 컴포넌트와 동일하게
	// AddComponent<T>()로 m_components에 넣고, 캐시 포인터만 따로 쥔다
	// (Entity.h m_pTransformComponent 주석 참고). SetParentID(0)은 원래
	// 동작 그대로 — 기본 생성 오브젝트는 자기 m_parentIndex는 -1이어도
	// Transform의 부모ID는 씬 루트(0)로 둔다(Transform.cpp 주석 — 부모 없음과
	// 동치 취급).
	m_pTransformComponent = AddComponent<Transform>();
	m_pTransformComponent->SetParentID(0);
}

Entity::Entity(Scene* scene, std::string_view name, GameObjectType type, Entity::Index index, Entity::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index),
    m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
	AttachSpatialComponent(type, parentIndex);
}

Entity::Entity(Scene* scene, size_t instanceID, std::string_view name, GameObjectType type, Entity::Index index, Entity::Index parentIndex) :
	Object(name, instanceID),
	m_gameObjectType(type),
	m_index(index),
	m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
	m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
	AttachSpatialComponent(type, parentIndex);
}

// ★ S3 — 공간 컴포넌트는 계열당 하나다. 단, 경계는 UI이지 Canvas가 아니다.
//
// UI는 rect로 배치되고 트랜스폼 행렬을 쓰지 않는다 — Scene::UpdateModelRecursive가
// `case GameObjectType::UI:`에서 아무 일도 하지 않고 자식 순회만 잇는 것이 그 증거다.
// 저작 자산 2,070 노드 중 UI가 680개(32.9%)이고, 그 전부가 아무도 갱신하지 않는
// 공간 데이터를 지고 있었다(실측).
//
// ★ Canvas는 다르다 — 처음엔 UI와 함께 묶었다가 되돌렸다. 근거:
// UpdateModelRecursive의 특례는 UI **하나뿐**이고 Canvas는 `default:` 분기를 타서
// 월드 행렬이 실제로 계산된다. 그 값을 `CanvasRenderMode::WorldSpace` 경로가
// canvasWorld로 읽는다(UIProxyBridge.cpp·ProxyCommand.cpp 두 곳). Canvas에서
// Transform을 빼면 월드 공간 캔버스가 원점에 붙는다 — 저작 자산은 지금 전부
// ScreenSpaceOverlay라 회귀에 안 잡히지만, 지원되는 모드를 조용히 깨는 것이다.
// (이 사실은 Transform_()의 널 폴백 로그가 잡아냈다 — Canvas 5종이 찍혔다.
//  정적 분석은 UIButton 한 곳만 찾았고 이 둘은 놓쳤다.)
//
// 두 생성자가 같은 규칙을 쓰도록 한 함수로 모은다 — 예전엔 같은 블록이 두 벌
// 복사돼 있었고, 한쪽만 고치면 "코드로 만든 것"과 "파일에서 연 것"이 갈린다.
void Entity::AttachSpatialComponent(GameObjectType type, Entity::Index parentIndex)
{
	if (GameObjectType::UI == type)
	{
		AddComponent<RectTransformComponent>();
		return;
	}

	if (GameObjectType::Canvas == type)
	{
		// 캔버스는 둘 다 갖는다 — rect는 자식 레이아웃의 기준, Transform은
		// 월드 공간 배치용. 상호배타의 예외이고, 그 이유가 위 주석이다.
		AddComponent<RectTransformComponent>();
	}

	m_pTransformComponent = AddComponent<Transform>();
	m_pTransformComponent->SetParentID(parentIndex);
}

const std::string& Entity::RemoveSuffixNumberTag() const
{
	// ����ǥ����: ���� ���� " (����)" �Ǵ� "(����)" ���� ����
	return m_removedSuffixNumberTag;
}

void Entity::SetTag(std::string_view tag)
{
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

    if (TagManager::GetInstance()->HasTag(tag))
    {
            m_tag = tag.data();
    }
}

void Entity::SetLayer(std::string_view layer)
{
    if (layer.empty())
    {
        return;
    }

    if (TagManager::GetInstance()->HasLayer(layer))
    {
		size_t layerIndex = TagManager::GetInstance()->GetLayerIndex(layer); // Ensure layer is registered
        m_layer = layer.data();
		m_collisionType = (uint32)layerIndex;
    }
}

void Entity::Destroy()
{
	if (m_destroyMark)
	{
		return;
	}

	TagManager::GetInstance()->RemoveTagFromObject(m_tag.ToString(), this);
	TagManager::GetInstance()->RemoveObjectFromLayer(m_layer.ToString(), this);

	// 프리팹 인스턴스 목록에서 뺀다. 넣기만 하고 빼는 곳이 없어서 죽은 포인터가
	// 목록에 남았고, 다음 UpdateInstances가 그것을 역참조했다.
	PrefabUtilitys->UnregisterInstance(this);

	m_destroyMark = true;
	TypeTrait::GUIDCreator::EraseGUID(m_instanceID);
#ifndef DYNAMICCPP_EXPORTS
	// 스크립트 핸들 무효화의 정본 지점(SceneGraphRedesignPlan 트랙 E4 — G1의 임시
	// 배선을 여기로 회수·확정한다). Scene::ReleaseSlot(슬롯 해제 단일점)이 아니라
	// 여기인 이유: ReleaseSlot은 진짜 파괴(DestroyGameObjects)와 DDOL 이송
	// (DetachGameObjectHierarchy)이 공유하는데, 후자는 오브젝트가 살아서 다른 씬으로
	// 옮겨가는 것뿐이라 스크립트 핸들이 죽으면 안 된다. Destroy()는 자식까지
	// 재귀하는 유일한 진짜 파괴 API이고 DDOL 이송은 이 경로를 타지 않으므로,
	// 여기서 Unregister하면 "진짜 파괴"만 정확히 걸러진다 — N-4가 이 함수 하나로
	// 구조적으로 재발 불가능해진다(코드베이스 전체에서 Unregister 호출은 이
	// 줄뿐이다).
	ScriptObjectRegistry::Get().Unregister(this);
#endif

	for (auto& component : m_components)
	{
		if (!component) continue;

		// ★ Transform은 파괴 마크에서 뺀다 (S1-b) — 수명이 GameObject와 같다.
		//
		// 마크하면 프레임 끝 Scene::DestroyComponents가 m_components에서 지우고,
		// RefreshComponentIdIndices→RebuildComponentTypeMask가 캐시
		// m_pTransformComponent를 널로 되돌린다. 그런데 씬 파괴는 그 뒤에도
		// 계층을 손보며 SetParentIndex를 부른다(Scene::DestroyGameObjects) —
		// 거기서 널 역참조로 죽었다(실측: 씬 전환 중 ACCESS_VIOLATION, 쓰기 주소 0xB0).
		// Transform은 개별 제거 대상이 아니라 오브젝트의 일부다. 여기서 마크하지
		// 않으면 m_components가 소멸할 때(오브젝트와 함께) 정상적으로 사라진다.
		if (component.get() == m_pTransformComponent) continue;

		component->Destroy();
	}

	for (auto& childIndex : m_childrenIndices)
	{
		Entity* child = FindIndex(childIndex);
		if (child)
		{
			child->Destroy();
		}
	}
}

void Entity::AttachComponentLifecycle(Component* component)
{
    if (!component) return;

    Scene* scene = this->GetScene();
    if (nullptr == scene) return;

    // 소유자는 아직 안 붙었을 수 있지만(호출부마다 순서가 다르다) 등록은 typeID만
    // 보므로 무관하다 — 소유자는 Awake를 부를 때 확인한다.
    scene->RegisterComponent(component);
}

Component* Entity::AddComponent(const Meta::Type& type)
{
    if (auto it = std::ranges::find_if(m_components, [&](const std::unique_ptr<Component>& component) { return component->GetTypeID() == type.typeID; }); it != m_components.end())
    {
		Debug->LogWarning("Component of type " + type.name + " already exists on Entity " + m_name.ToString() + ". Only one instance allowed.");
		return it->get();
    }

    // CT11: 팩토리 접합 — 이미 손에 쥔 Type이 생성 함수를 직접 든다(조회 0회).
    // K2 스테이지 A: make_shared 경로(createShared) 대신 createUnique로 만든다 —
    // GameObject가 유일한 소유자이므로 shared_ptr의 참조 계수는 애초에 필요 없었다.
    // void* → Component*는 createShared의 shared_ptr<void> → static_pointer_cast<Component>와
    // 같은 원리(단일 상속 체인이라 오프셋 0, meta::polymorphic의 가상 소멸자가 올바른 파생
    // 타입으로 delete되게 한다).
    std::unique_ptr<Component> component = type.createUnique
        ? std::unique_ptr<Component>(static_cast<Component*>(type.createUnique().release()))
        : nullptr;

    Component* rawComponent = component.get();
    if (rawComponent)
    {
		rawComponent->SetOwner(this);

		AttachComponentLifecycle(rawComponent);

        m_components.push_back(std::move(component));

		// K2: m_componentIds(맵) 소멸 — push_back 자체가 등록이다. 조회는
		// FindComponentSlot(마스크 선판정 + 선형 탐색)으로 수렴했다.

		// K1-a 후속 배선: Meta::Type 경유 부착(디스크 로드 경로, ComponentFactory::
		// LoadComponent가 실제로 부른다)도 템플릿 AddComponent<T>()와 동일하게
		// 마스크 비트를 세워야 HasComponent<T>()가 로드된 오브젝트에서도 맞는다.
		const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(rawComponent->GetTypeID());
		if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid)
		{
			m_componentTypeMask |= (1ull << maskIndex);
		}
    }

	return rawComponent;
}

Component* Entity::AddComponentAllowMultiple(const Meta::Type& type)
{
	std::unique_ptr<Component> component = type.createUnique
		? std::unique_ptr<Component>(static_cast<Component*>(type.createUnique().release()))
		: nullptr;

	Component* rawComponent = component.get();
	if (!rawComponent)
	{
		return nullptr;
	}

	rawComponent->SetOwner(this);

	AttachComponentLifecycle(rawComponent);

	m_components.push_back(std::move(component));

	// K2: 타입→인덱스 맵(m_componentIds) 소멸 — 맵은 타입당 인덱스 하나만 담아서
	// 다중 부착 시 나머지 인스턴스를 표현할 수 없었다. FindComponentSlot의 선형
	// 탐색은 살아있는 인스턴스를 전부 정확히 찾으므로 이 블록 자체가 필요 없다.

	// K1-a 후속 배선: 다중 부착 경로(ScriptComponent 등)도 마스크를 세운다.
	// 이미 켜져 있으면(같은 타입 두 번째 이상 부착) 아무 효과 없는 OR라 안전하다.
	const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(rawComponent->GetTypeID());
	if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid)
	{
		m_componentTypeMask |= (1ull << maskIndex);
	}

	return rawComponent;
}

Component* Entity::GetComponent(const Meta::Type& type)
{
    // K2: m_componentIds(맵) 소멸 — FindComponentSlot(마스크 선판정 + 선형 탐색)로 수렴.
    const size_t slot = FindComponentSlot(type.typeID);
    return slot == kInvalidComponentSlot ? nullptr : m_components[slot].get();
}

void Entity::RefreshComponentIdIndices()
{
	// K2: m_componentIds(맵) 소멸 — 정본은 m_components 하나뿐이라 재구축할
	// 인덱스맵이 없다. 이 함수는 컴포넌트 벡터가 통째로 재배치된 뒤 불리므로
	// (Scene::DestroyComponents가 호출) 마스크만 처음부터 다시 세운다.
	RebuildComponentTypeMask();
}

void Entity::AddChild(Entity* _objcet)
{
	auto scene = SceneManagers->GetActiveScene();
	if (!scene || !_objcet) return;

	// _objcet의 이전 부모가 없으면(최상위 오브젝트) 씬 루트의 children에서 자신을
	// 뗀다 — CreateGameObject 등 "부모 미지정 = 루트"인 관례를 여기서도 명시한다.
	// Scene::GetGameObject의 루트 폴백이 예전엔 이 자리에서도 암묵적으로 같은
	// 일을 했다(N-13) — 폴백이 사라진 지금은 무효 핸들을 nullptr로 돌려주므로
	// 직접 채워야 한다.
	auto oldParent = scene->GetGameObject(_objcet->m_parentIndex);
	if (!oldParent)
	{
		oldParent = scene->GetRootObject();
	}

	if (oldParent)
	{
		oldParent->DetachChildIndex(_objcet->m_index);
	}

	_objcet->SetParentIndex(m_index);
	AttachChildIndex(_objcet->m_index);
}

// Transform 없는 오브젝트(S3의 UI/Canvas)에서 Transform_()가 불렸을 때의 폴백.
//
// 크래시 대신 "누가 불렀는지"를 남긴다 — S3의 전제("UI에 도달하는 Transform 접근은
// UIButton 하나뿐")는 정적 분석 결과라, 놓친 경로는 이 로그로만 드러난다.
// 오브젝트 이름별로 한 번씩만 찍는다(매 프레임 호출이면 로그가 묻힌다).
Transform& Entity::MissingTransformFallback(const Entity* who)
{
	static Transform s_dummy{};
	static std::unordered_set<std::string> s_reported;

	const std::string name = who ? who->m_name.ToString() : std::string("<null>");
	if (s_reported.insert(name).second)
	{
		Debug->LogError("[S3] Transform이 없는 오브젝트에서 Transform_()가 불렸다: '" + name
			+ "' — UI/Canvas는 RectTransformComponent만 갖는다. 이 호출부를 찾아 고쳐야 한다"
			" (지금은 공유 더미를 돌려주므로 값이 반영되지 않는다).");
	}
	return s_dummy;
}

void Entity::SetParentIndex(Entity::Index parentIndex)
{
	m_parentIndex = parentIndex;
	// 캐시가 널일 수 있는 유일한 구간은 파괴 진행 중이다(위 Destroy 주석 참고 —
	// 그 경로는 막았지만, 계층 정리가 트랜스폼 없이도 성립해야 한다는 사실 자체는
	// 여기에 남긴다). m_parentIndex는 이미 갱신됐으므로 계층 정합성은 유지된다.
	if (m_pTransformComponent) m_pTransformComponent->SetParentID(parentIndex);
}

void Entity::AttachChildIndex(Entity::Index childIndex)
{
	if (std::ranges::find(m_childrenIndices, childIndex) != m_childrenIndices.end()) return;
	m_childrenIndices.push_back(childIndex);
}

void Entity::DetachChildIndex(Entity::Index childIndex)
{
	std::erase(m_childrenIndices, childIndex);
}

void Entity::ClearChildren()
{
	m_childrenIndices.clear();
}

void Entity::RemoveComponentTypeID(uint32 typeID)
{
	// K2: m_componentIds(맵) 소멸 — FindComponentSlot(마스크 선판정 + 선형 탐색)로 수렴.
	const size_t slot = FindComponentSlot(typeID);
	if (slot != kInvalidComponentSlot)
	{
		m_components[slot]->Destroy();
	}
}

// ── 조회 9종 수렴의 단일 구현 (SceneGraphRedesignPlan §3 트랙 E, E3) ──
//
// 전역 Find*와 OwnerSceneFind*가 씬 소스만 다르고(활성 씬 vs m_ownerScene)
// 몸통이 완전히 같았다. 아래 넷으로 수렴하고, 공개 API는 각자의 씬을
// 넘겨 위임만 한다. 인덱스 조회는 Scene::TryGetGameObject가 범위·
// INVALID_INDEX 검사를 이미 해 주므로 그대로 맡긴다.

Entity* Entity::FindByNameInScene(Scene* scene, std::string_view name)
{
	if (!scene) return nullptr;
	return scene->GetGameObject(name).get();
}

Entity* Entity::FindByIndexInScene(Scene* scene, Entity::Index index)
{
	if (!scene) return nullptr;
	return scene->TryGetGameObject(index).get();
}

Entity* Entity::FindByInstanceIDInScene(Scene* scene, const HashedGuid& guid)
{
	if (!scene) return nullptr;

	auto& gameObjects = scene->m_SceneObjects;
	// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1) — free 리스트로 회수된
	// 슬롯이 재사용되기 전까지 m_SceneObjects에 계속 남는다.
	auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](const std::shared_ptr<Entity>& object)
	{
		return object && object->m_instanceID == guid;
	});

	return it != gameObjects.end() ? it->get() : nullptr;
}

Entity* Entity::FindByAttachedIDInScene(Scene* scene, const HashedGuid& guid)
{
	if (!scene) return nullptr;

	auto& gameObjects = scene->m_SceneObjects;
	// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1).
	auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](const std::shared_ptr<Entity>& object)
	{
		return object && object->m_attachedSoketID == guid;
	});

	return it != gameObjects.end() ? it->get() : nullptr;
}

Entity* Entity::Find(std::string_view name)
{
	return FindByNameInScene(SceneManagers->GetActiveScene(), name);
}

Entity* Entity::FindIndex(Entity::Index index)
{
	return FindByIndexInScene(SceneManagers->GetActiveScene(), index);
}

Entity* Entity::FindInstanceID(const HashedGuid& guid)
{
	return FindByInstanceIDInScene(SceneManagers->GetActiveScene(), guid);
}

Entity* Entity::FindAttachedID(const HashedGuid& guid)
{
	return FindByAttachedIDInScene(SceneManagers->GetActiveScene(), guid);
}

Entity* Entity::OwnerSceneFind(std::string_view name)
{
	return FindByNameInScene(m_ownerScene, name);
}

Entity* Entity::OwnerSceneFindIndex(Entity::Index index)
{
	return FindByIndexInScene(m_ownerScene, index);
}

Entity* Entity::SceneObjectAt(Entity::Index index) const
{
	// Entity.inl의 자식 순회 전용 우회 — inl이 Scene.h를 물지 않도록
	// 비템플릿으로 여기서 대신 조회한다. 범위·tombstone 검사는
	// Scene::TryGetGameObject에 맡긴다(트랙 E3 — 예전엔 무검사로
	// m_SceneObjects를 직접 인덱싱했다).
	if (!m_ownerScene) return nullptr;
	return m_ownerScene->TryGetGameObject(index).get();
}

Entity* Entity::OwnerSceneFindInstanceID(const HashedGuid& guid)
{
	return FindByInstanceIDInScene(m_ownerScene, guid);
}

Entity* Entity::OwnerSceneFindAttachedID(const HashedGuid& guid)
{
	return FindByAttachedIDInScene(m_ownerScene, guid);
}

void Entity::SetEnabled(bool able)
{
	if (m_isEnabled == able)
	{
		return;
	}
	m_isEnabled = able;

	for (auto& component : m_components)
	{
		if (component)
		{
			component->SetEnabled(able);
		}
	}

	for (auto& childObjIndex : m_childrenIndices)
	{
		if (!m_ownerScene) continue;
		// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1) — children 목록은
		// 파괴 단일점이 정리해 주지만, 방어적으로 한 번 더 확인한다.
		if (!Entity::IsValidIndex(childObjIndex) ||
			static_cast<size_t>(childObjIndex) >= m_ownerScene->m_SceneObjects.size())
		{
			continue;
		}

		auto& childObj = m_ownerScene->m_SceneObjects[childObjIndex];
		if (childObj)
		{
			childObj->SetEnabled(able);
		}
	}
}

void Entity::SetCollisionType()
{
	size_t index = TagManager::GetInstance()->GetLayerIndex(m_layer.ToString());
	if (index >= TagManager::GetInstance()->GetLayers().size() || index > 32)
	{
		Debug->LogError("Invalid layer index: " + std::to_string(index));
		return;
	}

	m_collisionType = (uint32)index; // Set the bit corresponding to the layer index
}

void Entity::RebuildComponentTypeMask()
{
	// 선언은 Entity.h — 여기 있는 이유(순환 회피)도 그쪽 주석에 있다.
	m_componentTypeMask = 0;
	m_pTransformComponent = nullptr;
	for (const auto& component : m_components)
	{
		if (!component) continue;

		if (!m_pTransformComponent)
		{
			m_pTransformComponent = dynamic_cast<Transform*>(component.get());
		}

		const uint32_t index = TypeTrait::ComponentTypeIndex::Find(component->GetTypeID());
		if (index != TypeTrait::ComponentTypeIndex::kInvalid)
		{
			m_componentTypeMask |= (1ull << index);
		}
	}
}
