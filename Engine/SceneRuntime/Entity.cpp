#include "PhysicsBodyComponent.h"
#include "CharacterMovementComponent.h"
#include "Entity.h"
#include "EntityLayerSchema.h"
#include "AuthoringNodeViewAccess.h" // D3-a-4
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "TagManager.h"
#include "RectTransformComponent.h"
#include "Canvas.h"
#include "BoneComponent.h"
#include "PrefabUtility.h"
#include "ScriptObjectRegistry.h"
#include "ComponentFactory.h"
#include <stdexcept>

Entity::Entity() : Object("Entity"), m_index(0)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
}

Entity::Entity(Scene* scene, std::string_view name, GameObjectType,
    Entity::Index index, Entity::Index) : Object(name), m_index(index), m_ownerScene(scene)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
}

Entity::Entity(Scene* scene, size_t instanceID, std::string_view name, GameObjectType,
    Entity::Index index, Entity::Index) : Object(name, instanceID), m_index(index), m_ownerScene(scene)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<Entity>() };
}

gc::root_ref<Entity> Entity::Create(gc::domain& domain)
{
    auto entity = gc::make<Entity>(domain);
    entity->InitializeManaged(domain, GameObjectType::Empty, kSceneRootIndex);
    return entity;
}

gc::root_ref<Entity> Entity::Create(gc::domain& domain, Scene* scene,
    std::string_view name, GameObjectType type, Index index, Index parentIndex)
{
    if (scene && (scene->IsManagedRetiringOrRetired() || &scene->ManagedDomain() != &domain))
    {
        throw std::logic_error("Entity factory requires a live Scene in the same GC domain");
    }
    auto entity = gc::make<Entity>(domain, scene, name, type, index, parentIndex);
    entity->InitializeManaged(domain, type, parentIndex);
    return entity;
}

gc::root_ref<Entity> Entity::Create(gc::domain& domain, Scene* scene, size_t instanceID,
    std::string_view name, GameObjectType type, Index index, Index parentIndex)
{
    if (scene && (scene->IsManagedRetiringOrRetired() || &scene->ManagedDomain() != &domain))
    {
        throw std::logic_error("Entity factory requires a live Scene in the same GC domain");
    }
    auto entity = gc::make<Entity>(domain, scene, instanceID, name, type, index, parentIndex);
    entity->InitializeManaged(domain, type, parentIndex);
    return entity;
}

void Entity::InitializeManaged(gc::domain& domain, GameObjectType type, Index parentIndex)
{
    if (m_gcDomain || !root_from_this())
    {
        throw std::logic_error("Entity must be initialized once, after gc::make publication");
    }
    m_gcDomain = &domain;
    try
    {
        AttachSpatialComponent(type, parentIndex);
        if (!m_pTransformComponent)
        {
            m_missingTransformFallback = Component::CreateManaged<Transform>(domain);
        }
    }
    catch (...)
    {
        // A later spatial/fallback allocation can fail after an earlier
        // component was registered. Roll back those raw schedule observers
        // before releasing the factory's root; no lifecycle hook has run yet.
        if (m_ownerScene)
        {
            for (const auto& component : m_components)
            {
                m_ownerScene->UnregisterComponent(component.get());
            }
        }
        FinalizeManagedDestroy();
        throw;
    }
}

gc::domain& Entity::Domain() const
{
    if (!m_gcDomain)
    {
        throw std::logic_error("Entity allocation requires Entity::Create");
    }
    return *m_gcDomain;
}

void Entity::gc_trace(gc::tracer& tracer) const
{
    tracer.visit(m_components);
    tracer.visit(m_missingTransformFallback);
    // Scene/Prefab/hierarchy/transform-cache are borrowed observers, not owners.
}

void Entity::BeginManagedCleanup()
{
    gc::begin_cleanup_obligation(root_from_this());
}

void Entity::FinalizeManagedDestroy()
{
    auto self = root_from_this();
    if (gc::lifecycle_of(self) == gc::lifecycle_state::destroyed)
    {
        return;
    }
    gc::advance_lifecycle(self, gc::lifecycle_state::destroy_requested);
    gc::advance_lifecycle(self, gc::lifecycle_state::destroying);
    for (const auto& component : m_components)
    {
        if (component)
        {
            component->FinalizeManagedDestroy();
        }
    }
    m_components.clear();
    if (m_missingTransformFallback)
    {
        m_missingTransformFallback->FinalizeManagedDestroy();
    }
    m_missingTransformFallback.reset();
    m_pTransformComponent = nullptr;
    m_componentTypeMask = 0;
    m_ownerScene = nullptr;
    m_prefab = nullptr;
    m_destroyMark = true;
    gc::advance_lifecycle(self, gc::lifecycle_state::destroyed);
}

// ★ S3 — 공간 컴포넌트는 계열당 하나다. 단, 경계는 UI이지 Canvas가 아니다.
//
// UI는 rect로 배치되고 트랜스폼 행렬을 쓰지 않는다. E7-c 이후
// Scene::UpdateModelRecursive는 저장 타입을 보지 않고 HasTransform() 선판정으로
// 이 경계를 가른다.
//
// ★ Canvas는 다르다 — 처음엔 UI와 함께 묶었다가 되돌렸다. 근거:
// Canvas는 Transform을 가져 UpdateModelRecursive의 일반 공간 분기에서 월드 행렬이
// 실제로 계산된다. 그 값을 `CanvasRenderMode::WorldSpace` 경로가
// canvasWorld로 읽는다(UIProxyBridge.cpp·ProxyCommand.cpp 두 곳). Canvas에서
// Transform을 빼면 월드 공간 캔버스가 원점에 붙어 지원되는 모드를 조용히 깨뜨린다.
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

void Entity::OnAfterSerialize(const Authoring::MutableNodeView& view) const
{
	// 기본 팩토리/리플렉션 골든처럼 Scene 밖에 있는 Entity에는 계층이 없다.
	// 가짜 기본값 블록을 만들지 않아 "attached Store만 직렬화한다"는 경계를 지킨다.
	if (!m_ownerScene) return;
	// D3-a-5: 뷰를 그대로 넘긴다 — 이 함수는 노드를 열어 볼 이유가 없다.
	m_ownerScene->SerializeEntityHierarchy(*this, view);
}

const std::string& Entity::RemoveSuffixNumberTag() const
{
	// 정규표현식: 끝에 오는 " (숫자)" 또는 "(숫자)" 패턴 제거
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
			m_tag = tag;
    }
}

void Entity::OnBeforeDeserialize(const Authoring::NodeView& view) const
{
    const auto project = SceneManagers->ProjectLayers();
    if (!project || !ce::layers::ReadEntityLayer(Authoring::NodeViewAccess::Node(view), project->Snapshot()->catalog))
        throw std::runtime_error("Entity layer schema requires a registered stable ID; migrate legacy authoring first");
}

ce::layers::result<void> Entity::SetLayer(std::string_view name)
{
    const auto project = SceneManagers->ProjectLayers();
    if (!project)
        return std::unexpected(ce::layers::error::invalid_definition);

    const auto snapshot = project->Snapshot();
    const auto* layer = snapshot->catalog.Find(name);
    if (!layer)
        return std::unexpected(ce::layers::error::unknown_layer);

    return SetLayer(layer->id);
}

ce::layers::result<void> Entity::SetLayer(ce::layers::layer_id layer)
{
    if (m_ownerScene && m_ownerScene->Resolve(m_ownerScene->HandleOf(m_index)) == this)
        return m_ownerScene->AssignLayer(*this, layer);

    const auto project = SceneManagers->ProjectLayers();
    if (!project || !project->Snapshot()->catalog.Find(layer))
        return std::unexpected(ce::layers::error::unknown_layer);

    m_layerId = layer.value;
    return {};
}

void Entity::Destroy()
{
	// 씬 루트는 Scene과 수명을 함께한다. 슬롯 해제만 막으면 파괴 표시가
	// 남아 Play 복원 뒤 spatial resolver가 루트의 자식 전체를 건너뛴다.
	if (m_ownerScene && m_index == kSceneRootIndex
		&& m_ownerScene->GetEntityRaw(kSceneRootIndex) == this)
	{
		return;
	}

	if (m_destroyMark)
	{
		return;
    }

    TagManager::GetInstance()->RemoveTagFromObject(m_tag.ToString(), this);

    // 프리팹 인스턴스 목록에서 뺀다. 넣기만 하고 빼는 곳이 없어서 죽은 포인터가
    // 목록에 남았고, 다음 UpdateInstances가 그것을 역참조했다.
    PrefabUtilitys->UnregisterInstance(this);

	m_destroyMark = true;
    gc::advance_lifecycle(root_from_this(), gc::lifecycle_state::destroy_requested);
	TypeTrait::GUIDCreator::EraseGUID(m_instanceID);

	// ★ 스크립트 핸들은 여기서 죽이지 않는다 — Scene::DestroyEntities로 옮겼다.
	//
	// 여기서 Unregister하면 표시 시점에 핸들이 죽는데, 축소 삼단
	// (OnEndSimulation → OnRemovingFromScene → OnUninitializing)은 그보다 **뒤**인
	// Scene::FlushPendingDestroy에서 발화한다. 그래서 스크립트가 자기 마지막 훅을
	// 받는 동안 이미 자기 오브젝트에 닿을 수 없었다 — Entity.Name은 빈 문자열,
	// GetComponent는 전부 무응답. 크래시가 아니라 '정리 코드가 조용히 아무 일도
	// 하지 않는' 모습이라 알아채기 어렵다(2026-09-05 프로브 실측).
	//
	// 옮긴 자리가 DDOL 이송을 여전히 배제한다: DestroyEntities는 파괴 표시된
	// 엔티티만 훑고, 이송은 DetachEntityHierarchy가 ReleaseSlot을 직접 부르므로
	// 그 루프를 지나지 않는다. 즉 이 함수 주석이 지키려던 "진짜 파괴만 걸러낸다"는
	// 성질은 그대로고, 무효화 시점만 표시에서 실제 해제로 미뤄진다.

	for (auto& component : m_components)
	{
		if (!component) continue;

		// ★ Transform은 파괴 마크에서 뺀다 (S1-b) — 수명이 GameObject와 같다.
		//
		// 마크하면 프레임 끝 Scene::DestroyComponents가 m_components에서 지우고,
		// RefreshComponentIdIndices→RebuildComponentTypeMask가 캐시
		// m_pTransformComponent를 널로 되돌린다. 그런데 씬 파괴는 그 뒤에도
		// 계층을 손보며 SetParentIndex를 부른다(Scene::DestroyEntities) —
		// 거기서 널 역참조로 죽었다(실측: 씬 전환 중 ACCESS_VIOLATION, 쓰기 주소 0xB0).
		// Transform은 개별 제거 대상이 아니라 오브젝트의 일부다. 여기서 마크하지
		// 않으면 m_components가 소멸할 때(오브젝트와 함께) 정상적으로 사라진다.
		if (component.get() == m_pTransformComponent) continue;

		component->Destroy();
	}

	for (const Index childIndex : GetChildrenIndices())
	{
		Entity* child = OwnerSceneFindIndex(childIndex);
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
    // 보므로 무관하다 — 소유자는 OnInitialized를 부를 때 확인한다.
    scene->RegisterComponent(component);

	// X4 projection membership은 계층뿐 아니라 공간 컴포넌트 조합에도 달렸다.
	// 생성자 안에서는 아직 Scene 슬롯 점유 전이라 create mutation 하나로 충분하고,
	// 이미 점유된 Entity의 동적 부착만 별도 topology publication으로 올린다.
	if ((dynamic_cast<Transform*>(component)
		|| dynamic_cast<RectTransformComponent*>(component)
		|| dynamic_cast<Canvas*>(component)
		|| dynamic_cast<BoneComponent*>(component)
		|| dynamic_cast<MeshRenderer*>(component))
		&& scene->HandleOf(m_index).IsValid())
	{
		scene->RecordExecutionGraphMembershipChanged();
	}
}

bool Entity::CanAttachComponentType(const HashedGuid& type) const
{
    if (m_destroyMark || (m_ownerScene && m_ownerScene->IsManagedRetiringOrRetired()))
    {
        return false;
    }
    if (type == type_guid(CharacterMovementComponent))
        return FindComponentSlot(type_guid(PhysicsBodyComponent)) == kInvalidComponentSlot &&
            FindComponentSlot(type_guid(CharacterMovementComponent)) == kInvalidComponentSlot;
    if (type == type_guid(PhysicsBodyComponent))
        return FindComponentSlot(type_guid(CharacterMovementComponent)) == kInvalidComponentSlot;
    return true;
}

Component* Entity::PublishManagedComponent(const gc::root_ref<Component>& component, bool initialize)
{
    Component* raw = component.get();
    try
    {
        // Own the edge before publishing a raw scheduler observer. The local
        // root remains live through every throwing allocation/initialization.
        m_components.emplace_back(component);
        raw->SetOwner(this);
        if (auto* transform = dynamic_cast<Transform*>(raw))
        {
            m_pTransformComponent = transform;
        }
        AttachComponentLifecycle(raw);
        const uint32_t maskIndex = TypeTrait::ComponentTypeIndex::Find(raw->GetTypeID());
        if (maskIndex != TypeTrait::ComponentTypeIndex::kInvalid)
        {
            m_componentTypeMask |= (1ull << maskIndex);
        }
        if (initialize)
        {
            if (auto* initializable = dynamic_cast<System::IInitializable*>(raw))
            {
                initializable->Initialize();
            }
        }
        return raw;
    }
    catch (...)
    {
        if (m_ownerScene)
        {
            m_ownerScene->UnregisterComponent(raw);
        }
        raw->FinalizeManagedDestroy();
        std::erase_if(m_components, [raw](const gc::trace_ref<Component>& entry)
        {
            return entry.get() == raw;
        });
        RebuildComponentTypeMask();
        throw;
    }
}

Component* Entity::AddComponent(const reflgen::type_descriptor& type)
{
    const HashedGuid typeID = Meta::TypeIDOf(type);
    if (auto* existing = FindComponent(typeID))
    {
        Debug::PrintLog(spdlog::level::warn, "Component of type " + std::string(type.name())
            + " already exists on Entity " + m_name.ToString() + ". Only one instance allowed.");
        return existing;
    }
    if (!CanAttachComponentType(typeID))
    {
        return nullptr;
    }
    auto component = ComponentFactorys->CreateManaged(Domain(), type);
    return PublishManagedComponent(component, false);
}

Component* Entity::AddComponentAllowMultiple(const reflgen::type_descriptor& type)
{
    if (!CanAttachComponentType(Meta::TypeIDOf(type)))
    {
        return nullptr;
    }
    auto component = ComponentFactorys->CreateManaged(Domain(), type);
    return PublishManagedComponent(component, false);
}

Component* Entity::GetComponent(const reflgen::type_descriptor& type)
{
    // K2: m_componentIds(맵) 소멸 — FindComponentSlot(마스크 선판정 + 선형 탐색)로 수렴.
    const size_t slot = FindComponentSlot(Meta::TypeIDOf(type));
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
	if (!_objcet || !m_ownerScene || _objcet->GetScene() != m_ownerScene) return;
	m_ownerScene->Reparent(
		m_ownerScene->HandleOf(_objcet->m_index),
		m_ownerScene->HandleOf(m_index));
}

// Transform 없는 오브젝트(S3의 UI)에서 Transform_()가 불렸을 때의 폴백.
//
// 크래시 대신 "누가 불렀는지"를 남긴다 — S3의 전제("UI에 도달하는 Transform 접근은
// UIButton 하나뿐")는 정적 분석 결과라, 놓친 경로는 이 로그로만 드러난다.
// 오브젝트 이름별로 한 번씩만 찍는다(매 프레임 호출이면 로그가 묻힌다).
Transform& Entity::MissingTransformFallback(const Entity* who)
{
    if (!who || !who->m_missingTransformFallback)
    {
        throw std::logic_error("Transform requested on an uninitialized or retired Entity");
    }
    std::call_once(who->m_missingTransformReported, [who]
    {
        Debug::PrintLog(spdlog::level::err,
            "Transform requested on UI Entity '" + who->m_name.ToString()
            + "'; writes affect only its diagnostic fallback");
    });
    return *who->m_missingTransformFallback;
}

Entity::Index Entity::GetParentIndex() const
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		const auto& store = m_ownerScene->GetHierarchyStore();
		const size_t slot = static_cast<size_t>(m_index);
		if (store.IsOccupied(slot)) return store.ParentOf(slot);
	}
	return kInvalidIndex;
}

Entity::Index Entity::GetRootIndex() const
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		const auto& store = m_ownerScene->GetHierarchyStore();
		const size_t slot = static_cast<size_t>(m_index);
		if (store.IsOccupied(slot)) return store.RootOf(slot);
	}
	return kSceneRootIndex;
}

const std::vector<Entity::Index>& Entity::GetChildrenIndices() const
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		const auto& store = m_ownerScene->GetHierarchyStore();
		const size_t slot = static_cast<size_t>(m_index);
		if (store.IsOccupied(slot)) return store.ChildrenOf(slot);
	}
	static const std::vector<Index> empty;
	return empty;
}

void Entity::SetParentIndex(Entity::Index parentIndex)
{
	// 캐시가 널일 수 있는 유일한 구간은 파괴 진행 중이다(위 Destroy 주석 참고 —
	// 그 경로는 막았지만, 계층 정리가 트랜스폼 없이도 성립해야 한다는 사실 자체는
	// 여기에 남긴다). 계층 정본은 Transform과 별개인 Scene Store에 기록된다.
	if (m_pTransformComponent) m_pTransformComponent->SetParentID(parentIndex);
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		const size_t slot = static_cast<size_t>(m_index);
		m_ownerScene->m_hierarchyStore.SetParent(slot, parentIndex);
	}
}

void Entity::AttachChildIndex(Entity::Index childIndex)
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		m_ownerScene->m_hierarchyStore.AttachChild(static_cast<size_t>(m_index), childIndex);
	}
}

void Entity::DetachChildIndex(Entity::Index childIndex)
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		m_ownerScene->m_hierarchyStore.DetachChild(static_cast<size_t>(m_index), childIndex);
	}
}

void Entity::ClearChildren()
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		m_ownerScene->m_hierarchyStore.ClearChildren(static_cast<size_t>(m_index));
	}
}

void Entity::SetChildrenIndices(std::vector<Index> children)
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		m_ownerScene->m_hierarchyStore.SetChildren(static_cast<size_t>(m_index), std::move(children));
	}
}

void Entity::SetRootIndex(Index rootIndex)
{
	if (m_ownerScene && Entity::IsValidIndex(m_index)
		&& m_ownerScene->GetEntityRaw(m_index) == this)
	{
		m_ownerScene->m_hierarchyStore.SetRoot(static_cast<size_t>(m_index), rootIndex);
	}
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
// 넘겨 위임만 한다. 인덱스 조회는 Scene::TryGetEntity가 범위·
// kInvalidIndex 검사를 이미 해 주므로 그대로 맡긴다.

Entity* Entity::FindByNameInScene(Scene* scene, std::string_view name)
{
	if (!scene) return nullptr;
	return scene->GetEntity(name);
}

Entity* Entity::FindByIndexInScene(Scene* scene, Entity::Index index)
{
	if (!scene) return nullptr;
	return scene->TryGetEntity(index);
}

Entity* Entity::FindByInstanceIDInScene(Scene* scene, const HashedGuid& guid)
{
	if (!scene) return nullptr;

	auto& gameObjects = scene->m_Entities;
	// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1) — free 리스트로 회수된
	// 슬롯이 재사용되기 전까지 m_Entities에 계속 남는다.
	auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](const gc::trace_ref<Entity>& object)
	{
		return object && object->m_instanceID == guid;
	});

	return it != gameObjects.end() ? it->get() : nullptr;
}

Entity* Entity::FindByAttachedIDInScene(Scene* scene, const HashedGuid& guid)
{
	if (!scene) return nullptr;

	auto& gameObjects = scene->m_Entities;
	// tombstone(nullptr) 슬롯이 상시 존재한다(트랙 E1).
	auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [&](const gc::trace_ref<Entity>& object)
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

Entity* Entity::EntityAt(Entity::Index index) const
{
	// Entity.inl의 자식 순회 전용 우회 — inl이 Scene.h를 물지 않도록
	// 비템플릿으로 여기서 대신 조회한다. 범위·tombstone 검사는
	// Scene::TryGetEntity에 맡긴다(트랙 E3 — 예전엔 무검사로
	// m_Entities를 직접 인덱싱했다).
	if (!m_ownerScene) return nullptr;
	return m_ownerScene->TryGetEntity(index);
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

	// ★ 인덱스로 돈다 — 이 루프 안에서 m_components가 자랄 수 있다.
	//
	// 지금까지는 OnEnable/OnDisable을 구현한 컴포넌트가 0개라 이 루프가 사용자
	// 코드를 부르는 일이 없었다. ScriptComponent가 그 둘을 override하면서
	// (활성 축 배선) 스크립트의 OnDisable이 이 한복판에서 돈다 — 거기서
	// AddComponent를 부르면 push_back이 참조 순회를 무효화한다.
	//
	// 매 바퀴 크기를 다시 읽으므로 도중에 늘어난 컴포넌트도 같은 전이를 받는다.
	// 그것이 옳다: 꺼지는 오브젝트에 붙은 것은 꺼진 채로 시작해야 한다.
	for (size_t i = 0; i < m_components.size(); ++i)
	{
		if (Component* component = m_components[i].get())
		{
			component->SetEnabled(able);
		}
	}

	for (const Index childObjIndex : GetChildrenIndices())
	{
		// EntityAt은 소유 Scene의 범위와 tombstone을 함께 검사한다.
		if (Entity* childObj = EntityAt(childObjIndex))
		{
			childObj->SetEnabled(able);
		}
    }
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
