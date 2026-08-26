#include "Scene.h"
#include <cstdio> // FireReentrancyStress가 stdout에도 낸다(회귀가 발화를 본다)
#include "LifecycleRegistry.h"
#include "VolumeComponent.h"
#include "LifecycleTrace.h"
#include "Entity.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "LightComponent.h"
#include "MeshRenderer.h"
#include "SpriteRenderer.h"
#include "Terrain.h"
#include "RenderScene.h"
#include "MathematicsInterop.h"
#include "Animator.h"
#include "AnimatorSystem.h"
#include "DecalSystem.h"
#include "FoliageSystem.h"
#include "UITickSystem.h"
#include "SoundSystem.h"
#include "PlayerInputSystem.h"
#include "LightSystem.h"
#include "CameraSystem.h"
#include "CameraComponent.h"
#include "CharacterControllerSystem.h"
#include "Skeleton.h"
#include "BoneComponent.h"
#include "PhysicsManager.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "MeshCollider.h"
#include "CharacterControllerComponent.h"
#include "FoliageComponent.h"
#include "TerrainCollider.h"
#include "RigidBodyComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "TagManager.h"
#include "UIManager.h"
#include "PlayerInput.h"
#include "DecalComponent.h"
#include "RectTransformComponent.h"
#include "SpriteSheetComponent.h"
#include "AIManager.h"
#include <execution>
#include <queue>
#include <algorithm>
#include <ranges>
#include <iterator>
#include <atomic> // TryEnterTraversal의 깊이초과 1회 보고 플래그(std::atomic<bool>)에 필요 — 전이 include에 기대지 않음

using namespace std::literals;

#include "Profiler.h"

// ===== 유틸: 중복 없이 push_back =====
template<class R, class T>
static bool push_unique(R& vec, const T& v)
{
    if (std::ranges::find(vec, v) == vec.end())
    {
        vec.push_back(v);
        return true;
    }
    return false;
}

Scene::Scene()
    // 씬 식별자(트랙 W)는 생성자에서 딱 한 번 받는다 — Scene은 복사·이동이
    // 불가능한 타입이라(Scene.h의 m_sceneId 주석 참고) 이 값이 인스턴스 생애
    // 내내 유일하다는 전제가 깨지지 않는다.
    : m_sceneId(NextSceneId())
{
    resetObjHandle = SceneManagers->resetSelectedObjectEvent.AddRaw(this, &Scene::ResetSelectedEntity);
    m_Entities.reserve(3000);
    m_generations.reserve(3000);
    m_hierarchyStore.Reserve(3000);
}

// 씬 생성마다 단조 증가하는 일련번호(Scene.h의 m_sceneId 주석 — Skeleton::NextSerial
// 선례와 같은 패턴). 1부터 시작한다 — 0은 EntityHandle의 "무효/미지정"과 겹치면
// 안 되므로 건너뛴다.
uint32_t Scene::NextSceneId()
{
    static std::atomic<uint32_t> counter{ 1 };
    return counter.fetch_add(1, std::memory_order_relaxed);
}

Scene::~Scene()
{
	DrainAIUpdate();
    SceneManagers->resetSelectedObjectEvent -= resetObjHandle;
    // 생명주기 델리게이트 15종의 Clear 연쇄가 여기 있었다(PHASE 9-3에서 철거).
    //
    // 종료 행의 자리이기도 했다: Clear가 콜백을 파괴하는데 그 파괴가 같은 델리게이트의
    // Remove를 다시 부르면 재진입 불가 스핀락에서 영원히 돌았다(커밋 c712011f).
    // 델리게이트가 없으니 그 연쇄 자체가 성립하지 않는다.
    //
    // 리스트는 비우기만 하면 된다 — 원소가 raw 포인터라 소멸자 연쇄가 없다.
    m_schedule.Clear();

    m_entityNameSet.clear();
    m_lightComponents.clear();
    m_allMeshRenderers.clear();
    m_staticMeshRenderers.clear();
    m_skinnedMeshRenderers.clear();
    m_lightSlots.clear();
    m_terrainComponents.clear();
    m_foliageComponents.clear();
    m_decalComponents.clear();
    m_spriteRenderers.clear();
    m_Entities.clear();
    m_generations.clear();
    m_freeSlots.clear();
    m_hierarchyStore.Clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// 슬롯맵 (SceneGraphRedesignPlan 트랙 E1)
// ─────────────────────────────────────────────────────────────────────────────
//
// 예전에는 DestroyEntities가 파괴마다 생존자 전원의 인덱스를 재부여했다
// (N-6) — 여기 세 함수가 그것을 대체한다. 생존자의 인덱스는 이제 파괴가
// 일어나도 절대 바뀌지 않는다.
Entity::Index Scene::AllocateSlot()
{
    if (!m_freeSlots.empty())
    {
        Entity::Index index = static_cast<Entity::Index>(m_freeSlots.back());
        m_freeSlots.pop_back();
        return index;
    }

    Entity::Index index = static_cast<Entity::Index>(m_Entities.size());
    m_Entities.push_back(nullptr);
    m_generations.push_back(1);
    // 트랜스폼 스토어를 슬롯맵과 평행하게 늘린다(트랙 S, S1) — 프리리스트
    // 재사용 슬롯은 이미 ReleaseSlot이 초기값으로 되돌려 놨으므로 여기 오지 않는다.
    m_transformStore.GrowOne();
    m_hierarchyStore.GrowOne();
    return index;
}

std::unique_ptr<Entity> Scene::ReleaseSlot(Entity::Index index)
{
    // 루트(0)는 씬 자체가 서 있는 동안 절대 해제하지 않는다.
    if (index == 0) return {};
    if (index < 0 || static_cast<size_t>(index) >= m_Entities.size()) return {};

    // ── 여기서 ScriptObjectRegistry를 건드리면 안 된다(SceneGraphRedesignPlan
    // 트랙 E4 검토 결과) ──
    //
    // 이 함수는 DestroyEntities(진짜 파괴)와 DetachEntityHierarchy(DDOL
    // 이송 — 오브젝트는 살아서 다른 씬으로 옮겨갈 뿐)가 공유하는 슬롯 해제
    // 단일점이다. "슬롯 해제 지점에서 관리 핸들 무효화가 함께 일어난다"가
    // 설계 문서의 원칙이라 여기서 스크립트 핸들도 죽이고 싶어질 수 있지만, 그러면
    // DDOL 이송 중에도 핸들이 죽는다 — 그리고 그 이송 창(Detach 직후·재부착
    // 이전) 동안 실제로 SceneManager::LoadSceneImmediate가
    // ClrHost::NotifySceneUnload를 부르고, 그 안에서
    // BehaviourRegistry.SweepOrphans가 모든 활성 Behaviour의 GameObject.IsAlive를
    // 확인한다(ScriptCore/BehaviourRegistry.cs:324, "살아 있다 — DDOL 포함"). 여기서
    // 핸들을 무효화하면 살아있는 DDOL 스크립트가 씬 전환마다 고아로 오판되어
    // 뜯겨나간다. 스크립트 핸들 무효화의 정본 지점은 대신 GameObject::Destroy()다
    // (진짜 파괴만 지나가는, 재귀까지 포함하는 유일한 경로 — Entity.cpp 참고).
    std::unique_ptr<Entity> released = std::move(m_Entities[index]);

    // 트랜스폼 스토어 슬롯 리셋(트랙 S, S1) — Transform::ResolveStore의 점유자
    // 확인이 이 시점부터 실패하므로(m_Entities[index]가 비었다) 이 리셋을
    // 하지 않아도 낡은 데이터를 읽을 위험은 없지만, 다음 입주자가 재사용
    // 슬롯을 잡았을 때 곧바로 깨끗한 값을 보게 여기서 미리 되돌려 둔다.
    m_transformStore.ResetSlot(static_cast<size_t>(index));
    m_hierarchyStore.ResetSlot(static_cast<size_t>(index));

    // 세대 0은 EntityHandle의 "무효"와 겹치므로 건너뛴다.
    ++m_generations[index];
    if (0 == m_generations[index])
    {
        m_generations[index] = 1;
    }
    m_freeSlots.push_back(static_cast<uint32_t>(index));
	return released;
}

void Scene::SerializeEntityHierarchy(const Entity& entity, YAML::Node& node) const
{
	if (!Entity::IsValidIndex(entity.m_index)) return;
	const size_t index = static_cast<size_t>(entity.m_index);
	if (index >= m_Entities.size() || m_Entities[index].get() != &entity) return;
	if (!m_hierarchyStore.IsOccupied(index)) return;

	// 디스크 스키마는 H3 이전과 동일하게 유지한다. 달라진 것은 값의 출처다:
	// Entity 멤버가 아니라 Scene-owned Store에서 세 키를 보충한다.
	node["m_parentIndex"] = m_hierarchyStore.ParentOf(index);
	node["m_rootIndex"] = m_hierarchyStore.RootOf(index);
	YAML::Node children(YAML::NodeType::Sequence);
	children.SetStyle(YAML::EmitterStyle::Flow);
	for (Entity::Index child : m_hierarchyStore.ChildrenOf(index))
	{
		children.push_back(child);
	}
	node["m_childrenIndices"] = children;
}

size_t Scene::CountHierarchyStoreMismatches() const
{
	size_t mismatches = 0;
	if (m_hierarchyStore.Size() != m_Entities.size())
		++mismatches;

	for (size_t index = 0; index < m_Entities.size(); ++index)
	{
		const bool hasEntity = static_cast<bool>(m_Entities[index]);
		if (hasEntity != m_hierarchyStore.IsOccupied(index)) ++mismatches;
	}
	return mismatches;
}

void Scene::DrainAIUpdate()
{
	if (m_AIFuture.valid())
		m_AIFuture.get();
}

void Scene::UnlinkFromParentChildren(Entity::Index index)
{
    if (!Entity::IsValidIndex(index) || static_cast<size_t>(index) >= m_Entities.size())
        return;

    auto& node = m_Entities[index];
    if (!node) return;

    const Entity::Index parentIndex = node->GetParentIndex();
    if (Entity::IsValidIndex(parentIndex))
    {
        if (Entity* parent = TryGetEntity(parentIndex))
        {
            parent->DetachChildIndex(index);
        }
    }
    // 최상위 오브젝트는 부모 인덱스가 무효인 채로 씬 루트의 children에만 들어
    // 있다(N-13 이전부터의 관례) — 그래서 무조건 한 번 더 시도한다.
    if (!m_Entities.empty() && m_Entities[0])
    {
        m_Entities[0]->DetachChildIndex(index);
    }
}

Entity* Scene::Resolve(EntityHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    // 씬 스코프 검사(트랙 W) — index+generation이 우연히 맞아도 다른 씬 것이면
    // 즉시 거른다. m_generations/m_Entities는 씬마다 독립이라 이 검사
    // 없이는 "다른 씬의 같은 슬롯"을 구조적으로 막을 수 없다(EntityHandle.h
    // 상단 주석 참고).
    if (handle.sceneId != m_sceneId) return nullptr;
    if (handle.index >= m_generations.size()) return nullptr;
    if (m_generations[handle.index] != handle.generation) return nullptr;
    if (handle.index >= m_Entities.size()) return nullptr;

    return m_Entities[handle.index].get();
}

EntityHandle Scene::HandleOf(Entity::Index index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_generations.size())
        return EntityHandle{};
    if (static_cast<size_t>(index) >= m_Entities.size() || !m_Entities[index])
        return EntityHandle{};

    return EntityHandle{ m_sceneId, static_cast<uint32_t>(index), m_generations[index] };
}

Entity* Scene::AddEntity(std::unique_ptr<Entity> sceneObject)
{
	if (!sceneObject) return nullptr;
    std::string uniqueName = GenerateUniqueEntityName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);
    sceneObject->m_ownerScene = this;
	if (Transform* transform = sceneObject->GetComponent<Transform>())
	{
		transform->SetDirty();
	}

    Entity::Index index = AllocateSlot();
    Entity* added = sceneObject.get();
	m_Entities[index] = std::move(sceneObject);

    added->m_index = index;
	m_hierarchyStore.OccupySlot(static_cast<size_t>(index));

    if (Entity* root = GetRootEntity(); root && root != added)
    {
        added->SetParentIndex(root->m_index);
        root->AttachChildIndex(added->m_index);
    }

    if (!added->m_tag.ToString().empty())
    {
        TagManagers->AddTagToObject(added->m_tag.ToString(), added);
    }

    if (!added->m_layer.ToString().empty())
    {
        TagManagers->AddObjectToLayer(added->m_layer.ToString(), added);
    }

    return added;
}

void Scene::AddRootEntity(std::string_view name)
{
    std::string uniqueName{};

    if (name.empty())
    {
        uniqueName = GenerateUniqueEntityName("SampleScene");
    }
    else
    {
        uniqueName = GenerateUniqueEntityName(name);
    }

    Entity::Index index = AllocateSlot();
    auto ptr = std::make_unique<Entity>(this, uniqueName, GameObjectType::Empty, index, -1);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return;
    }

    m_Entities[index] = std::move(ptr);
	m_hierarchyStore.OccupySlot(static_cast<size_t>(index), Entity::INVALID_INDEX,
		Entity::kSceneRootIndex);
}

Entity* Scene::CreateEntity(std::string_view name, GameObjectType type, Entity::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }

    // ★ 루트 규약(트랙 E · E8의 잔여, 2026-08-20 교정).
    //
    // 예전에는 여기서 -1을 넣었다. 그런데 아래에서 **루트의 children에는 넣는다** —
    // 즉 "부모가 없다면서 루트 children에 실려 있는" 상태를 이 함수가 매번
    // 만들고 있었다. E8이 저작 자산과 Attach/리맵 경로에서 없앤 바로 그 상태다.
    //
    // 같은 파일의 LoadEntity는 이미 kSceneRootIndex를 쓴다 — **두 생성 경로가
    // 서로 다른 규약을 쓰고 있었다.** 저작 자산은 로드 경로를 타므로 0으로
    // 정규화돼 있었고, 그래서 저작 씬만 재던 verify-hierarchy-convention은 이
    // 위반을 볼 수 없었다(갓 만든 씬은 잰 적이 없다). CLI 저작본으로 게이트를
    // 옮기다 드러났다 — 왕복에서 -1이 0으로 바뀌어 트랜스폼 다이제스트가 어긋났다.
    if (parentIndex >= m_Entities.size())
    {
        parentIndex = Entity::kSceneRootIndex;
    }

    std::string uniqueName = GenerateUniqueEntityName(name);

    Entity::Index index = AllocateSlot();

    auto ptr = std::make_unique<Entity>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return nullptr;
    }
    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    Entity* created = ptr.get();
	m_Entities[index] = std::move(ptr);
	m_hierarchyStore.OccupySlot(static_cast<size_t>(index), parentIndex,
		Entity::kSceneRootIndex);

    // parentIndex가 무효면(부모를 명시하지 않은 보통의 호출) 씬 루트를 부모로
    // 삼는다 — GetEntity의 루트 폴백이 암묵적으로 하던 일을 여기서 명시한다.
    // 폴백은 이 의도된 경우와 진짜 오염된 인덱스를 구분하지 못하고 둘 다 조용히
    // root로 흡수했다(N-13) — 여기서는 의도된 경우만 root로 보내고, 나머지는
    // parentObj가 nullptr로 남아 아래 if에서 걸러진다.
    Entity* parentObj = GetEntity(parentIndex);
    if (!parentObj)
    {
        parentObj = GetRootEntity();
    }
    if (parentObj && parentObj->m_index != index)
    {
        parentObj->AttachChildIndex(index);
        // 표기를 **실제로 붙은 부모**와 맞춘다(E8 불변식: 자식이 부모의 children에
        // 실린다 ⟺ 자식의 m_parentIndex가 그 부모다). 위 폴백으로 루트에 흡수된
        // 경우 — parentIndex가 범위 안이지만 그 슬롯이 비어 있던 경우 — 에도
        // 표기가 옛 값으로 남으면 그대로 쌍불일치가 된다.
        created->SetParentIndex(parentObj->m_index);
    }
    if (!created->m_tag.ToString().empty())
    {
        TagManager::GetInstance()->AddTagToObject(created->m_tag.ToString(), created);
    }

    if (!created->m_layer.ToString().empty())
    {
        TagManager::GetInstance()->AddObjectToLayer(created->m_layer.ToString(), created);
    }

    return created;
}

Entity* Scene::LoadEntity(size_t instanceID, std::string_view name, GameObjectType type, Entity::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }

    if (parentIndex >= m_Entities.size())
    {
        parentIndex = Entity::kSceneRootIndex;
    }

    std::string uniqueName = GenerateUniqueEntityName(name);

    Entity::Index index = AllocateSlot();
    auto ptr = std::make_unique<Entity>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return nullptr;
    }

    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    Entity* loaded = ptr.get();
	m_Entities[index] = std::move(ptr);
	m_hierarchyStore.OccupySlot(static_cast<size_t>(index), parentIndex,
		Entity::kSceneRootIndex);

    return loaded;
}

Entity* Scene::GetEntity(Entity::Index index)
{
    // 예전에는 범위 밖 인덱스를 조용히 루트(m_Entities[0])로 흘려보냈다
    // (N-13) — 계층 오염이 몇 달을 숨어 있던 원인이다. 무효한 요청은 이제
    // TryGetEntity와 같은 의미로 nullptr을 돌려준다.
    if (index >= 0 && static_cast<size_t>(index) < m_Entities.size())
    {
        return m_Entities[index].get();
    }

    return nullptr;
}

Entity* Scene::TryGetEntity(Entity::Index index)
{
    if (index == Entity::INVALID_INDEX || index < 0)
    {
        return nullptr;
    }
    if (static_cast<size_t>(index) < m_Entities.size())
    {
        return m_Entities[index].get();
    }
    return nullptr;
}

void Scene::DetachEntityHierarchy(Entity* root, std::vector<DetachedEntityTransfer>& detached)
{
    if (!root) return;
    Scene* origin = root->GetScene();
    if (origin != this) return;
    // 씬 루트(0)는 이 경로로 오면 안 된다 — 아래에서 슬롯 해제 단일점을 타므로,
    // 다른 파괴 경로와 마찬가지로 여기서도 방어적으로 막는다.
    if (0 == root->m_index) return;
	DrainAIUpdate();

    // breadth-first (인덱스 재배열 없이 안전하게 순회)
    std::vector<Entity::Index> queue;
	const Entity::Index rootIndex = root->m_index;
    queue.push_back(rootIndex);

    // 루트부터 부모/씬 루트 children 에서 분리
	UnlinkFromParentChildren(rootIndex);
	root->SetParentIndex(Entity::INVALID_INDEX);

    for (size_t qi = 0; qi < queue.size(); ++qi)
    {
        auto idx = queue[qi];
        Entity* node = TryGetEntity(idx);
        if (!node) continue;

        // 자식 enqueue: 유효 인덱스만 복사
        std::ranges::copy_if(
            node->GetChildrenIndices(),
            std::back_inserter(queue),
            [](Entity::Index i) { return Entity::IsValidIndex(i); }
        );

		// ReleaseSlot은 HierarchyStore 슬롯도 즉시 reset한다. 재부착에 필요한
		// 원본 관계는 그 전에 캡처해야 하며, detached Entity의 shadow 필드를
		// 다시 읽는 경로를 만들지 않는다.
		DetachedEntityTransfer transfer{};
		transfer.oldIndex = idx;
		transfer.oldParentIndex = node->GetParentIndex();
		transfer.oldRootIndex = node->GetRootIndex();

        // 태그/레이어에서 분리 (원 씬 검색에서 빠지도록)
        if (!node->m_tag.ToString().empty())
        {
            TagManager::GetInstance()->RemoveTagFromObject(node->m_tag.ToString(), node);
        }
        if (!node->m_layer.ToString().empty())
        {
            TagManager::GetInstance()->RemoveObjectFromLayer(node->m_layer.ToString(), node);
        }
		if (Transform* transform = node->GetComponent<Transform>())
			transform->CaptureSceneTransferState();

        // 씬 이탈 통지(트랙 L1). 대칭짝은 AttachExistingEntity의 OnAddedToScene.
        // DDOL 이송은 오브젝트를 살려 둔 채 씬만 바꾸는 희귀 경로라, 기록이 없으면
        // "이송 때 이 훅이 돌았는가"를 기준선으로 확인할 방법이 없다(C5).
        for (auto& component : node->m_components)
        {
            if (!component) continue;
            LIFECYCLE_TRACE(Lifecycle::Phase::OnRemovingFromScene,
                Lifecycle::Trace::TypeNameOf(component.get()),
                node->m_name.ToString().c_str(), component->GetInstanceID());
            // 명시 통지는 없앴다 — ScriptComponent가 이 훅을 오버라이드하므로 위
            // 호출 하나가 이송과 파괴 양쪽을 덮는다(트랙 L · L3 완결). 파괴 경로에서
            // 관리 측 TearDown과 겹치는 문제는 관리 측 '축소 전달됨' 상태가 가른다.
            component->OnRemovingFromScene();
        }
        node->m_scenePhase = ScenePhase::Attached;
		node->m_ownerScene = nullptr;

        // 슬롯 해제 단일점(트랙 E1) — tombstone+세대 증가+free 리스트 등록을
        // DestroyEntities와 공유한다. 재부착은 AttachExistingEntity가
		if (std::unique_ptr<Entity> owned = ReleaseSlot(idx))
		{
			transfer.entity = std::move(owned);
			detached.push_back(std::move(transfer));
		}
    }
}

// === C안 구현: 이름 충돌 방지 ===
std::string Scene::MakeUniqueName(std::string_view base)
{
    std::string name(base);
    if (name.empty()) name = "Entity";
    if (!GetEntity(name)) return name;
    int n = 1;
    std::string trial;
    do {
        trial = name + " (" + std::to_string(n++) + ")";
    } while (GetEntity(trial));
    return trial;
}

// === C안 구현: 단일 객체 부착 ===
Entity::Index Scene::AttachExistingEntity(std::unique_ptr<Entity> go, Entity::Index parentIndex)
{
    if (!go) return Entity::INVALID_INDEX;
	Entity* object = go.get();

    // 이 씬 기준 유니크 네임 보장
	if (Entity* existed = GetEntity(object->GetHashedName().ToString()); existed)
		object->SetName(MakeUniqueName(object->GetHashedName().ToString()));

    // 이 씬에 소속
	object->m_ownerScene = this;

    // 새 인덱스 할당 — free 리스트가 있으면 재사용한다(트랙 E1).
    Entity::Index newIndex = AllocateSlot();
	object->m_index = newIndex;
	m_Entities[newIndex] = std::move(go);
	m_hierarchyStore.OccupySlot(static_cast<size_t>(newIndex));
	if (Transform* transform = object->GetComponent<Transform>())
		transform->RestoreSceneTransferState();

    // Tag/Layer 재등록
    if (!object->m_tag.ToString().empty())
		TagManager::GetInstance()->AddTagToObject(object->m_tag.ToString(), object);
    if (!object->m_layer.ToString().empty())
		TagManager::GetInstance()->AddObjectToLayer(object->m_layer.ToString(), object);

    // Transform 부모 세팅.
    //
    // ★ 루트 규약이 여기서 갈렸다 (SceneGraphRedesignPlan 트랙 E, 2026-08-20 통일).
    // 예전 주석은 "INVALID_INDEX == 루트"였고 Entity::AddChild는 실제 인덱스(루트면 0)를
    // 넣었다. 둘 다 씬 루트의 children에는 들어가므로 "부모가 없다면서 루트 children에
    // 실려 있는" 상태가 정상처럼 보였고, 저작 자산에 두 표기가 236 대 31로 섞였다.
    // 그 어긋남이 순회가 서브트리를 통째로 빠뜨리는 결함의 뿌리다(뼈 61개).
    //
    // 이제 표기는 하나다: **부모의 children에 실린다 <=> 그 부모를 m_parentIndex로 가리킨다.**
    // 최상위 오브젝트도 예외가 아니고, 그 부모는 씬 루트(kSceneRootIndex)다.
    // INVALID_INDEX는 "어느 씬에도 붙어 있지 않다"만 뜻한다(DDOL 이탈 중인 오브젝트).
    // scene.hierarchycheck가 이 불변식을 잰다.
    if (Entity::IsValidIndex(parentIndex))
    {
		object->SetParentIndex(parentIndex);
		if (Entity* parent = TryGetEntity(parentIndex))
        {
            parent->AttachChildIndex(newIndex);
        }
    }
    else if (!m_Entities.empty() && m_Entities[0])
    {
		object->SetParentIndex(Entity::kSceneRootIndex);
        m_Entities[0]->AttachChildIndex(newIndex);
    }
    else
    {
        // 씬 루트조차 없는 상태 — 붙일 곳이 없으므로 무부모로 둔다.
		object->SetParentIndex(Entity::INVALID_INDEX);
    }

    // 씬 편입 통지(트랙 L1) — DDOL 재부착은 이미 초기화가 끝난 컴포넌트가
    // 대부분이라 pendingAwake 큐(이미 지난 정거장)를 다시 타지 않는다. 대칭짝은
    // DetachEntityHierarchy의 OnRemovingFromScene. 이 자리도 기록한다(C5).
	object->m_scenePhase = ScenePhase::InScene;
	for (auto& component : object->m_components)
    {
        if (!component) continue;
        LIFECYCLE_TRACE(Lifecycle::Phase::OnAddedToScene,
            Lifecycle::Trace::TypeNameOf(component.get()),
			object->m_name.ToString().c_str(), component->GetInstanceID());
        // 명시 통지는 없앴다 — ScriptComponent가 이 훅을 오버라이드하므로 위 호출
        // 하나가 이송과 신규 생성 양쪽을 덮는다(트랙 L · L3 잔여 2단계).
        // 대칭짝인 DetachEntityHierarchy 쪽은 아직 명시 통지가 남아 있다:
        // OnRemovingFromScene을 가상으로 받으면 **모든 파괴**에서도 불려 관리 측
        // TearDown과 이중 발화한다(사유: ScriptLifecyclePhase.h).
        component->OnAddedToScene();
    }

    // 필요 시 컴포넌트 쪽 씬/이벤트 갱신은 호출측(매니저)에서 일괄 처리
    return newIndex;
}

// === C안 구현: 서브트리 부착 ===
std::unordered_map<Entity::Index, Entity::Index>
Scene::AttachExistingEntityHierarchy(std::vector<DetachedEntityTransfer>& objects)
{
    std::unordered_map<Entity::Index, Entity::Index> remap;
    if (objects.empty()) return remap;
	struct PendingRootRemap
	{
		Entity::Index newIndex{ Entity::INVALID_INDEX };
		Entity::Index oldRootIndex{ Entity::INVALID_INDEX };
	};
	std::vector<PendingRootRemap> pendingRoots;
	pendingRoots.reserve(objects.size());

    // BFS로 루트별 서브트리 전개 (부모 → 자식 순서 보장)
	// 옛 Scene 슬롯/관계는 DetachEntityHierarchy가 Store에서 캡처한 transfer를
	// 사용한다. 원 Scene 슬롯은 이미 reset됐고 Entity shadow는 H3 호환용일 뿐이다.
	for (auto& transfer : objects)
    {
		if (!transfer.entity) continue;
        Entity::Index newParent =
			remap.contains(transfer.oldParentIndex) ? remap[transfer.oldParentIndex] :
            Entity::INVALID_INDEX;

		auto newIdx = AttachExistingEntity(std::move(transfer.entity), newParent);
		remap[transfer.oldIndex] = newIdx;
		pendingRoots.push_back({ newIdx, transfer.oldRootIndex });
    }

	// rootIndex는 부모와 별개의 same-scene 참조(주로 Bone palette root)다.
	// old slot을 그대로 두면 목적 Scene에서 우연히 다른 Entity를 가리킬 수 있으므로
	// 모든 슬롯 배정이 끝난 뒤 같은 remap으로 다시 연결한다.
	for (const PendingRootRemap& pending : pendingRoots)
	{
		Entity* entity = TryGetEntity(pending.newIndex);
		if (!entity) continue;

		Entity::Index newRoot = Entity::INVALID_INDEX;
		if (Entity::IsValidIndex(pending.oldRootIndex))
		{
			if (auto found = remap.find(pending.oldRootIndex); found != remap.end())
			{
				newRoot = found->second;
			}
			else if (Entity::kSceneRootIndex == pending.oldRootIndex)
			{
				newRoot = Entity::kSceneRootIndex;
			}
		}
		entity->SetRootIndex(newRoot);
	}
	objects.clear();
    return remap;
}

Entity* Scene::GetEntity(std::string_view name)
{
    HashingString hashedName(name.data());
    for (auto& obj : m_Entities)
    {
        if (obj && obj->GetHashedName() == hashedName)
        {
			return obj.get();
        }
    }
    return nullptr;
}

void Scene::DestroyEntity(Entity* sceneObject)
{
    if (nullptr == sceneObject)
    {
        return;
    }

    RemoveEntityName(sceneObject->GetHashedName().ToString());

    sceneObject->Destroy();
}

void Scene::DestroyEntity(Entity::Index index)
{
    if (index < m_Entities.size())
    {
        Entity* obj = m_Entities[index].get();
        if (nullptr != obj)
        {
            RemoveEntityName(obj->GetHashedName().ToString());
            obj->Destroy();
        }
    }
    else
    {
        return;
    }
}

// 렌더 프록시 커밋 (트랙 S · S4의 측정 대상).
//
// UpdateRenderData 안에 인라인으로 있던 것을 함수로 뽑았다 — 벤치가 이 단계만
// 따로 재려면 진입점이 필요하고, S4가 손볼 자리도 여기 하나다.
// 컴포넌트 목록은 값으로 복사한다(원본이 순회 중 바뀔 수 있다 — 기존 규약 유지).
void Scene::CommitRenderProxies()
{
    auto renderScene = SceneManagers->GetRenderScene();
    if (nullptr == renderScene) return;

    // 스냅샷은 스크래치 버퍼에 담는다 — 매 프레임 새 벡터를 만들지 않는다.
    //
    // 값 복사를 하는 이유는 그대로다(UpdateCommand 도중 원본 목록이 바뀔 수 있다).
    // 바뀐 것은 그 복사가 **어디에 담기는가**다. 예전에는 지역 벡터 8개를 매
    // 프레임 새로 만들어 힙 할당 8회를 무조건 물었다 — 실측에서 이 단계의
    // 고정 비용이 ~28µs였고, 저작 씬 대부분(렌더 컴포넌트 150개 미만)에서는
    // 그 고정분이 컴포넌트당 가변분(~0.19µs)의 합보다 컸다. assign은 멤버
    // 버퍼의 capacity를 재사용하므로 워밍업 이후 할당이 0이 된다.
    const auto snapshot = [](auto& dst, const auto& src)
    {
        dst.assign(src.begin(), src.end());
        return std::cref(dst);
    };

    auto& allMeshes            = snapshot(m_scratchMeshRenderers, m_allMeshRenderers).get();
    auto& terrainComponents    = snapshot(m_scratchTerrains, m_terrainComponents).get();
    auto& foliageComponents    = snapshot(m_scratchFoliages, m_foliageComponents).get();
    auto& imageComponents      = snapshot(m_scratchImages, UIManagers->Images).get();
    auto& textComponents       = snapshot(m_scratchTexts, UIManagers->Texts).get();
    auto& spriteRenderers      = snapshot(m_scratchSpriteRenderers, m_spriteRenderers).get();
    auto& spriteSheetComponents= snapshot(m_scratchSpriteSheets, UIManagers->SpriteSheets).get();
    auto& decalComponents      = snapshot(m_scratchDecals, m_decalComponents).get();

    const auto updateProxies = [&](auto& components, const char* label)
    {
        for (auto* component : components)
        {
            if (nullptr == component) continue;
            try
            {
                renderScene->UpdateCommand(component);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error updating " << label << " command: " << e.what() << '\n';
            }
        }
    };

    updateProxies(allMeshes, "mesh");
    updateProxies(terrainComponents, "terrain");
    updateProxies(foliageComponents, "foliage");
    updateProxies(decalComponents, "decal");
    updateProxies(spriteRenderers, "sprite");
    updateProxies(imageComponents, "image");
    updateProxies(textComponents, "text");
    updateProxies(spriteSheetComponents, "spriteSheet");
}

size_t Scene::RenderProxyComponentCount() const
{
    return m_allMeshRenderers.size() + m_terrainComponents.size() + m_foliageComponents.size()
        + m_decalComponents.size() + m_spriteRenderers.size()
        + UIManagers->Images.size() + UIManagers->Texts.size() + UIManagers->SpriteSheets.size();
}

void Scene::UpdateRenderData()
{
    InternalPauseUpdateForUI();

    // ── 렌더 프록시 갱신 (카메라와 무관) ──
    //
    // UpdateCommand는 카메라를 보지 않는다. 그런데 예전에는 이 호출들이 아래
    // 카메라 루프 안에 있어서 두 가지가 어긋나 있었다.
    //
    //   · 카메라가 N개면 같은 프록시를 N번 갱신했다.
    //   · 더 심각하게, 루프 머리의 RenderPassData 검사가 실패하면 continue가
    //     아니라 return이라 함수를 통째로 빠져나갔다. RenderPassData는 DX11
    //     SceneRenderer가 카메라마다 만들던 것이어서 DX12 단독 전환 뒤로는 늘
    //     비어 있었고, 그래서 갱신이 한 번도 실행되지 않았다 — 본 팔레트와
    //     월드 행렬이 프록시에 영영 도달하지 못해 스키닝 모델이 첫 포즈로
    //     굳었던 원인이다.
    //
    // 카메라와 무관한 일이므로 루프 밖에서 한 번만 돈다.
    //
    // 스레드풀에 넣지 않는다. ProxyCommand 생성자들이 RenderScene의 프록시·
    // 애니메이터 맵을 만지는데 그 락 규약은 아직 전수 검증되지 않았다(구조
    // 분석의 CRITICAL ①). 병렬화는 프로파일이 요구할 때 별도로 다룬다.
    PROFILE_CPU_BEGIN("CommitRenderProxies");
    CommitRenderProxies();
    PROFILE_CPU_END();

    // ── 워커 UI 푸시 파이프라인을 걷었다 (2026-08-20 전수 추적) ──
    //
    // 카메라마다 워커 태스크 셋이 UI 컴포넌트 instanceID를
    // RenderPassData::PushUIRenderData로 밀어 넣고 NotifyAllAndWait로 프레임을
    // 세웠지만, 그 버퍼(m_findUIProxyVec)는 4중으로 죽어 있었다:
    //   · 렌더 소비자 0 — 라이브 UI는 RenderScene::UIProxySnapshot에서 그린다
    //   · 큐 전달 0 — m_UIRenderQueue를 채우는 PushUIRenderQueue는 호출자 0
    //   · 회전 0 — 유일한 m_frame 증가점 AddFrame도 호출자 0이라 트리플 버퍼가
    //     돈 적이 없고, 읽기는 항상 빈 슬롯만 봤다
    //   · 클리어 0 — ClearUIRenderDataBuffer 호출자 0이라 푸시된 ID가 무한 축적
    // 즉 매 프레임 워커 팬아웃 비용과 누수만 내고 화면에는 아무 기여가 없었다.
    // 컬링 버퍼를 걷어낸 RenderSceneViewPlan ③과 같은 양식 — 생산만 있고 소비가 없다.
}

void Scene::InternalPauseUpdateForUI()
{
    if (SceneManagers->IsGamePaused())
    {
        float deltaTime = Time->GetElapsedSeconds();
        Entity* canvasObj = UIManagers->GetCurCanvas();
        if (!canvasObj) return;

        AllUIUpdateWorldMatrix();

        auto canvas = canvasObj->GetComponent<Canvas>();
		if (!canvas) return;
		for (const EntityHandle& handle : canvas->UIObjs)
        {
			Entity* obj = Resolve(handle);
            if (obj)
            {
                auto imageComponents = obj->GetComponents<ImageComponent>();
                for (const auto& imageComponent : imageComponents)
                {
                    // C3 4차: Update → TickLayout 개명. 이 자리를 안 고치면
                    // 기반의 빈 가상 Component::Update에 조용히 붙어 일시정지 중
                    // UI 배치가 멈춘다(컴파일 통과·크래시 없음).
                    imageComponent->TickLayout(deltaTime);
                }

                auto textComponents = obj->GetComponents<TextComponent>();
                for (const auto& textComponent : textComponents)
                {
                    // C3 레인 2: Update → TickLayout 개명. 이 자리를 안 고치면
                    // 기반의 빈 가상 Component::Update에 조용히 붙어 일시정지 중
                    // UI 배치가 멈춘다 — 컴파일도 되고 크래시도 없다(적대적 검토 발견).
                    textComponent->TickLayout(deltaTime);
                }

                auto spriteSheetComponents = obj->GetComponents<SpriteSheetComponent>();
                for (const auto& spriteSheetComponent : spriteSheetComponents)
                {
                    spriteSheetComponent->TickLayout(deltaTime);
                }

                auto inputComponents = obj->GetComponents<PlayerInputComponent>();
                for (const auto& inputComponent : inputComponents)
                {
                    // C3 — PlayerInputComponent가 TickInput으로 옮겨갔다. 이 자리를
                    // 안 고치면 기반의 빈 가상 Component::Update에 조용히 붙는다.
                    inputComponent->TickInput(deltaTime);
                }

            }
        }
    }
}

void Scene::Reset()
{
    // C++ 핫리로드 은퇴(9-4)로 비움 —
    // 관리 스크립트(ScriptComponent)는 ClrHost가 재부착을 스스로 처리한다.
}

// ─────────────────────────────────────────────────────────────────────────────
// 생명주기 레지스트리 (PHASE 9-1)
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // swap-and-pop 제거 헬퍼는 SystemSchedule.cpp로 옮겼다(트랙 C1) — 리스트가
    // 사는 곳이 옮겨 갔으니 제거 로직도 함께다. Scene.cpp 쪽 유일한 호출부였던
    // UnregisterComponent는 이제 m_schedule.UnsubscribeAll을 부른다.

    // 재진입 시험 상태 (PHASE 9-9). 게임 스레드에서만 만진다.
    bool g_stressArmed = false;
    int  g_stressKind  = 0;
    int  g_stressCount = 0;

}

// TraceTypeName은 Lifecycle::Trace::TypeNameOf로 승격됐다(LifecycleTrace.h) —
// C3로 틱이 시스템으로 옮겨가면서 Scene 밖에서도 같은 문자열을 남겨야 하는데,
// 익명 네임스페이스에 있으면 시스템들이 각자 복제하게 된다. 그러면 기준선 대조가
// "같은 문자열"이라는 전제부터 흔들린다.

void Scene::RegisterComponent(Component* component)
{
    if (nullptr == component) return;

    // 표가 비어 있으면 여기서 세운다.
    //
    // ComponentFactory::Initialize가 세우도록 해 뒀지만, 기동 시 기본 씬의
    // Main Camera·Directional Light는 그보다 먼저 만들어진다 — 그때 표가 비어 있어
    // 그 둘만 등록되지 않았고, A/B 대조가 OnDestroy 2건 유실로 잡아냈다.
    // 초기화 순서에 기대지 않는 편이 낫다.
    if (0 == Lifecycle::Registry::Count()) Lifecycle::Registry::RegisterAllComponents();

    const uint16_t mask = Lifecycle::Registry::Find(component->GetTypeID().m_ID_Data);

    if (Lifecycle::Registry::kUnregistered == mask)
    {
        // 조용히 넘어가지 않는다.
        //
        // 예전 CRTP에서는 이 상황("생명주기를 받아야 하는데 판정에서 빠졌다")이
        // '훅이 하나도 없는 타입'과 구분되지 않아 아무 일도 안 일어난 채 지나갔다.
        // 컴포넌트를 새로 만들고 LifecycleRegistry.cpp의 목록에 넣는 것을 잊으면
        // 여기서 이름과 함께 드러난다.
        Debug->LogError("[Lifecycle] 등록되지 않은 컴포넌트 타입: "
            + component->ToString() + " — LifecycleRegistry.cpp의 목록에 추가할 것");
        return;
    }

    if (Lifecycle::Bit_None == mask) return;  // 훅이 하나도 없는 타입 — 넣을 곳이 없다

    // 이미 Awake(OnInitialized)를 받은 컴포넌트는 큐에 넣지 않는다.
    //
    // 등록은 한 번이 아니다 — DDOL은 씬을 건널 때마다, 경로 전환은 그 시점에
    // 다시 등록한다. 상태를 보지 않으면 그때마다 Awake가 또 돈다.
    //
    // OnAddedToScene(트랙 L1, 신설 축)은 대응하는 옛 훅이 없어 자기 상태 비트가
    // 없다 — OnInitialized와 같은 자리(RegistryDrainAwakeAndStart의 첫 loop)에서
    // 함께 따라잡히므로 같은 조건으로 이 큐에 태운다.
    // 편입은 SystemSchedule::SubscribeImplicit을 거친다(트랙 C1·L4) — 저장소가
    // 어디 있는지는 이 함수가 몰라도 된다. 카운트만 "암묵"으로 표시된다.
    if ((mask & (Lifecycle::Bit_OnInitialized | Lifecycle::Bit_OnAddedToScene)) &&
        !component->HasLifecycleState(Component::State_AwakeCalled))
    {
        m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingAwake);
    }
    else if ((mask & Lifecycle::Bit_OnBeginSimulation) && !component->HasLifecycleState(Component::State_StartCalled))
    {
        m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingStart);
    }


    // 파괴 감시 목록 — OnUninitializing(옛 OnDestroy)뿐 아니라 OnEndSimulation·
    // OnRemovingFromScene도 파괴 시점(FlushPendingDestroy)에 같은 자리에서 함께
    // 발화하므로, 셋 중 하나라도 필요하면 감시 대상이다.
    if (mask & (Lifecycle::Bit_OnUninitializing | Lifecycle::Bit_OnEndSimulation | Lifecycle::Bit_OnRemovingFromScene))
    {
        m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::DestroyWatch);
    }
}

void Scene::UnregisterComponent(Component* component)
{
    if (nullptr == component) return;

    m_schedule.UnsubscribeAll(component);
}

Scene::RegistryCounts Scene::GetRegistryCounts() const
{
    // update·lateUpdate·fixedUpdate 세 필드는 트랙 C3 완결로 뺐다 — SystemSchedule이
    // 그 리스트 자체를 철거해서(SystemSchedule.h 클래스 주석), 여기서 크기를 잴
    // 대상이 없어졌다.
    return RegistryCounts{
        m_schedule.PendingAwakeList().size(), m_schedule.PendingStartList().size(),
    };
}

void Scene::RegistryDrainAwakeAndStart()
{
    // 큐를 통째로 옮겨 놓고 돈다.
    //
    // Awake 안에서 AddComponent를 부르면 새 컴포넌트가 pendingAwake에 들어오는데,
    // 원본을 순회 중이면 그 push_back이 순회를 무효화한다. 옮겨 두면 새로 들어온
    // 것은 이번 바퀴에 끼지 않고 다음 프레임에 처리된다 — 이것이 재진입 안전의
    // 핵심이고, 델리게이트 경로가 하지 못하던 것이다.
    std::vector<Component*> awaking;
    awaking.swap(m_schedule.PendingAwakeList());

    for (Component* component : awaking)
    {
        if (nullptr == component) continue;
        Entity* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled())
        {
            // 아직 비활성 — 다음 프레임에 다시 시도한다. 이미 편입된 구독을
            // 유지하는 것뿐이라 SubscribeImplicit(카운트는 집합이라 재삽입해도
            // 늘지 않는다)을 그대로 써서 "편입은 전부 이 경로로"를 지킨다.
            m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingAwake);
            continue;
        }

        component->MarkLifecycleState(Component::State_AwakeCalled);
        LIFECYCLE_TRACE(Lifecycle::Phase::OnInitialized, Lifecycle::Trace::TypeNameOf(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->OnInitialized();

        // Scene 진입 통지(트랙 L1) — phase catch-up. 지금 엔티티는 생성과 동시에
        // 씬에 들어가므로 이 조건은 사실상 항상 참이다. DDOL 재부착의
        // OnAddedToScene은 이미 초기화가 끝난 컴포넌트가 대부분이라 이 큐를 다시
        // 타지 않고 Scene::AttachExistingEntity가 직접 부른다(대칭짝은
        // DetachEntityHierarchy의 OnRemovingFromScene). 두 자리 모두 기록한다.
        if (owner->m_scenePhase >= ScenePhase::InScene)
        {
            LIFECYCLE_TRACE(Lifecycle::Phase::OnAddedToScene, Lifecycle::Trace::TypeNameOf(component),
                owner->m_name.ToString().c_str(), component->GetInstanceID());
            component->OnAddedToScene();
        }

        const uint16_t mask = Lifecycle::Registry::Find(component->GetTypeID().m_ID_Data);
        if ((mask & Lifecycle::Bit_OnBeginSimulation) && !component->HasLifecycleState(Component::State_StartCalled))
        {
            m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingStart);
        }
    }

    std::vector<Component*> starting;
    starting.swap(m_schedule.PendingStartList());

    for (Component* component : starting)
    {
        if (nullptr == component) continue;
        Entity* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled())
        {
            m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingStart);
            continue;
        }

        component->MarkLifecycleState(Component::State_StartCalled);
        LIFECYCLE_TRACE(Lifecycle::Phase::OnBeginSimulation, Lifecycle::Trace::TypeNameOf(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->OnBeginSimulation();
    }
}

void Scene::FireReentrancyStress(bool midTraversal, const std::string& origin)
{
    // 순회 한복판이다. 여기서 하는 일이 곧 R1·R2의 시험이다.
    //
    // 델리게이트 시절이라면 이 자리에서 파괴가 일어나면 브로드캐스트가 들고 있던
    // 콜백 복사본이 죽은 객체를 계속 불렀다(R1). 지금은 파괴가 표시만 남기고 실제
    // 해제는 프레임 끝 한 지점으로 미뤄지므로, 순회 중인 리스트는 흔들리지 않는다.
    // 그 주장을 ASan 아래에서 확인하는 것이 목적이다.
    g_stressArmed = false;  // 한 프레임에 한 번만

    const int count = g_stressCount;
    const StressKind kind = static_cast<StressKind>(g_stressKind);

    int destroyed = 0;
    if (StressKind::Destroy == kind || StressKind::Both == kind)
    {
        // 루트(0번)는 건드리지 않는다 — 씬 구조가 무너지면 이후가 전부 무의미해진다.
        for (size_t i = 1; i < m_Entities.size() && destroyed < count; ++i)
        {
            const auto& owned = m_Entities[i];
            if (!owned || owned->IsDestroyMark()) continue;
			DestroyEntity(owned.get());
            ++destroyed;
        }
    }

    int added = 0;
    if (StressKind::AddComponent == kind || StressKind::Both == kind)
    {
        // 순회 중 생성 + AddComponent.
        //
        // 처음에는 기존 오브젝트에 붙이려 했는데 0건으로 끝났다 — 한 타입은 오브젝트당
        // 하나라는 규칙에 걸려 대부분 걸러졌고, 그 사실이 로그에 '추가 0'으로만 남아
        // 시험이 도는지 아닌지 구분되지 않았다. 새로 만들어 붙이면 항상 성립하고,
        // 덤으로 '순회 중 Entity 생성'까지 함께 시험한다.
        //
        // 확인할 불변식(트랙 C3 이후): 새 컴포넌트는 pendingAwake로 가야 한다.
        // 예전에는 "지금 도는 update 리스트에 끼어들면 안 된다"는 조건도 있었다
        // — RegistryTick이 그 자리에서 update 리스트를 순회했기 때문이다. C3가
        // RegistryTick과 update/lateUpdate/fixedUpdate 리스트를 통째로 걷어낸
        // 지금은 그 조건이 성립할 자리 자체가 없다(SystemSchedule.h 클래스 주석
        // 참고) — 틱은 전용 시스템의 조밀 vector가 돌고, 그 vector는 이 스트레스가
        // 만지는 m_schedule과 무관하다.
        for (int n = 0; n < count; ++n)
        {
            auto created = CreateEntity("StressReentrant_" + std::to_string(n));
            if (!created) continue;
            created->AddComponent<VolumeComponent>();
            ++added;
        }
    }

    // update 리스트 크기는 더 이상 찍지 않는다 — 트랙 C3로 SystemSchedule에서
    // 그 리스트가 철거됐다(위 for문 앞 주석). 남은 pendingAwake만 진단으로 남긴다.
    //
    // 문구는 회귀 스크립트가 그대로 매치하는 계약이다 — "순회 중"/"리스트 비어
    // 순회 밖"의 두 어절은 트랙 C2-0 이전부터 있던 것을 그대로 유지했고(문자열
    // 변경은 하위 호환을 깬다), origin(예: "CameraSystem::Update" /
    // "Update 폴백")을 덧붙여 어느 시스템의 어느 루프·어느 페이즈에서 터졌는지도
    // 같은 줄에서 드러낼 수 있게 했다.
    const std::string fireLine =
        "[Lifecycle] 재진입 시험 발화(" + std::string(midTraversal ? "순회 중" : "리스트 비어 순회 밖")
        + " · " + origin
        + ") — 파괴 " + std::to_string(destroyed)
        + " · 생성+컴포넌트 " + std::to_string(added)
        + " (pendingAwake " + std::to_string(m_schedule.PendingAwakeList().size()) + ")";

    Debug->LogWarning(fireLine);

    // ★ stdout에도 같은 줄을 낸다 — 이게 없으면 회귀가 발화를 못 본다.
    //
    // Debug->LogWarning은 spdlog의 인메모리 싱크와 HTML 파일 싱크로만 가고
    // **프로세스 stdout에는 한 글자도 쓰지 않는다**. 그런데 회귀
    // (verify-asan-lifecycle.ps1)는 리다이렉트된 stdout을 문자열로 뒤진다.
    // 그래서 그 검사는 지금까지 "순회 한복판에서"를 세고 있었는데, 그 문구는
    // **무장 시점의 콘솔 확인 printf**에서 나오는 것이지 발화 로그가 아니다 —
    // 즉 "무장했다"를 세면서 주석에는 "발화했는지 확인한다"고 적혀 있었다.
    // 자가 재는 것과 이름이 어긋난 또 하나의 사례다(scene.traversalbench의
    // "분기 도달" 라벨과 같은 종류).
    //
    // 다른 진단(scene.traversalbench·scene.proxybench)이 이미 쓰는 관례대로
    // 두 싱크에 같은 문자열을 보낸다. 회귀는 이제 발화 자체와, 그것이 순회
    // 중이었는지 폴백이었는지까지 가려 볼 수 있다.
    std::printf("%s\n", fireLine.c_str());
}

void Scene::ArmReentrancyStress(StressKind kind, int count)
{
    g_stressKind = static_cast<int>(kind);
    g_stressCount = (count > 0) ? count : 1;
    g_stressArmed = true;
}

// 재진입 시험(9-9)의 순회 중 발화 지점 — 트랙 C2-0으로 시스템 루프 안에 되살렸다.
//
// C3 완결로 RegistryTick 자체가 사라지면서(3d8ff9a4) "스케줄 리스트를 돌며 틱을
// 디스패치한다"는 자리가 통째로 없어졌고, 그와 함께 이 시험의 "순회 중"이라는
// 조건을 만족시킬 자리도 사라졌었다 — PumpReentrancyStress만 남아 매 페이즈
// 진입부(루프 밖)에서 터뜨렸는데, 그건 "리스트가 텅 비어 애초에 위험이 없다"는
// 것과 구분되지 않는 시험이었다. 아래 함수가 그 이빨을 다시 채운다.
//
// 무장 안 됐을 때 비용은 bool 하나 읽는 것뿐이다 — CameraSystem::Update처럼
// 매 프레임 도는 시스템 루프 한복판에서 불리는 핫패스이기 때문이다.
void Scene::TryFireReentrancyStressMidTraversal(const char* systemName, const char* loopLabel)
{
    if (!g_stressArmed) return;  // 비무장 — 여기서 끝난다.

    FireReentrancyStress(true, std::string(systemName) + "::" + std::string(loopLabel));
}

// 재진입 시험(9-9)의 순회 밖 폴백 — 옛 RegistryTick 소멸 이후 유일한 발화 경로였다.
//
// 트랙 C2-0으로 CameraSystem::Update 한복판에 진짜 발화점이 생긴 지금도 이 폴백은
// 남긴다 — 이유는 그대로다: 앞선 시험이 카메라를 전부 파괴해 CameraSystem의
// m_cameras가 비면(또는 아직 Awake 전이라 처음부터 비어 있으면) 그 루프 자체가
// 안 돌아 순회 중 발화점을 영영 못 만난다. 그 경우에도 무장이 조용히 증발하지
// 않도록 각 페이즈 진입부에서 한 번 더 확인한다.
//
// 순서 규약: 순회 중 발화가 우선이다. Scene::Update는 이 함수를
// m_cameraSystem.Update(...) 호출 "뒤"에 두어, 카메라 루프가 이미 소비한 무장을
// 여기서 또 터뜨리는 일이 없게 했다(g_stressArmed는 발화 즉시 false로 내려가므로
// 뒤에 오는 호출은 자연히 no-op이다 — 같은 프레임에 둘 다 터지는 경우는 없다).
void Scene::PumpReentrancyStress(const char* phaseLabel)
{
    if (g_stressArmed) FireReentrancyStress(false, std::string(phaseLabel) + " 폴백");
}

void Scene::FlushPendingDestroy()
{
    // 프레임 끝의 유일한 파괴 지점.
    //
    // 여기가 유일하다는 것이 R1(순회 중 UAF)과 R2(즉시 파괴)를 동시에 닫는다 —
    // 순회하는 동안에는 리스트에서 아무것도 빠지지 않으므로, "순회 중인 것이 죽는"
    // 상황이 표현 자체가 불가능해진다. 유니티가 Destroy를 프레임 경계로 미루는 이유와 같다.
    if (m_schedule.DestroyWatchList().empty()) return;

    std::vector<Component*> doomed;
    for (Component* component : m_schedule.DestroyWatchList())
    {
        if (nullptr == component) continue;
        Entity* owner = component->GetOwner();
        const bool dying = (nullptr == owner) || owner->IsDestroyMark() || component->IsDestroyMark();
        if (dying) doomed.push_back(component);
    }

    for (Component* component : doomed)
    {
        Entity* owner = component->GetOwner();

        // 이름을 값으로 붙든다.
        //
        // ToString()은 값을 돌려주므로 그 c_str()을 const char*에 담으면 문장이
        // 끝나는 순간 뜬다. 실제로 기록에 빈 이름이 찍혀 드러났다 — 크래시가 아니라
        // '이름만 비는' 모습이라 눈으로는 알아채기 어려운 종류다.
        const std::string ownerName = (nullptr != owner) ? owner->m_name.ToString() : std::string("?");

        // 파괴 직전 축소(트랙 L1) — 실제 파괴 앞에 Simulation 종료·씬 이탈을 먼저
        // 통지한다. 셋 다 기록한다(C5): 예전에는 "대응하는 옛 훅이 없다"는 이유로
        // 앞의 둘을 기록에서 뺐는데, 그러면 기준선이 **그 훅이 돌았는지 자체를
        // 말하지 못한다.** L3이 컴포넌트 ~30종을 이 축으로 옮기고 나면 그 침묵이
        // 곧 검증 공백이 된다.
        LIFECYCLE_TRACE(Lifecycle::Phase::OnEndSimulation, Lifecycle::Trace::TypeNameOf(component), ownerName.c_str(), component->GetInstanceID());
        component->OnEndSimulation();

        LIFECYCLE_TRACE(Lifecycle::Phase::OnRemovingFromScene, Lifecycle::Trace::TypeNameOf(component), ownerName.c_str(), component->GetInstanceID());
        component->OnRemovingFromScene();

        LIFECYCLE_TRACE(Lifecycle::Phase::OnUninitializing, Lifecycle::Trace::TypeNameOf(component), ownerName.c_str(), component->GetInstanceID());
        component->OnUninitializing();

        UnregisterComponent(component);
    }
}

void Scene::DrainPendingLifecycle()
{
    // 이 드레인이 pendingStart까지 소진한다 — 그래서 뒤이어 Start를 따로 부르는
    // 단계가 없다(옛 Scene::Start는 그 사실을 적은 빈 함수였고 C4에서 지웠다).
    RegistryDrainAwakeAndStart();
}

void Scene::FixedUpdate(float deltaSecond)
{
    if (m_AIFuture.valid() && m_AIFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        m_AIFuture.get();
    }
    PROFILE_CPU_BEGIN("AllUpdateWorldMatrix");
    AllUpdateWorldMatrix();
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("SetInternalPhysicData");
    SetInternalPhysicData();
    PROFILE_CPU_END();

    // 트랙 C2-0 — 여기 있던 PumpReentrancyStress("FixedUpdate") 폴백 호출을 뺐다.
    //
    // Player::PlayerMain::Update가 매 프레임 SceneManagers->Physics(FixedUpdate가
    // 여기서 불린다)를 SceneManagers->GameLogic(Update·LateUpdate가 불린다) "앞에"
    // 무조건 부른다(PlayerMain.cpp:335-336, SceneManager.cpp:354-359·370-388 —
    // 고정 타임스텝 누산기로 걸러지는 게 아니라 매 프레임 정확히 한 번이다). 즉
    // 이 자리에 폴백을 두면 Update의 CameraSystem 루프가 한 번도 돌기 전에
    // 무장을 항상 먼저 가로챈다 — "순회 중 발화가 우선"이라는 순서 규약과
    // 정면으로 부딪힌다. 무장은 이제 Update에서만 소비된다(CameraSystem 루프의
    // 순회 중 지점이 우선이고, 그것이 못 잡으면 Update 안의 폴백이 같은 프레임
    // 안에서 바로 뒤이어 잡는다) — FixedUpdate 자체엔 순회 중 발화점이 없으므로
    // 여기서 더 할 일이 없다.
    PROFILE_CPU_BEGIN("internalfixedBroadcast");
    // 트랙 C3 잔여 — CharacterControllerComponent::FixedUpdate 이관분.
    // ★ 자리가 PhysicsManagers->Update **이전**이어야 한다. 옛 구현은
    // FixedUpdateList 안에서 Physics->AddInputMove 등으로 그 프레임의 이동 입력을
    // 큐에 실었고 바로 다음 물리 스텝이 그것을 같은 프레임에 소비했다 —
    // 순서가 뒤집히면 캐릭터 이동이 한 프레임 밀린다.
    CharacterControllerSystems->FixedUpdate(deltaSecond);
    PROFILE_CPU_END();
    // Internal Physics Update 작성
    PROFILE_CPU_BEGIN("physxUpdate");
    PhysicsManagers->Update(deltaSecond);
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("yield_WaitForFixedUpdate");
    // OnTriggerEvent.Broadcast(); 작성
    CoroutineManagers->yield_WaitForFixedUpdate();
    PROFILE_CPU_END();
}

namespace
{
    // C# 스크립트에 물리 콜백을 전달한다. 즉시 호출하지 않고 큐에만 담는다 —
    // 충돌마다 경계를 넘으면 "틱당 1회" 원칙이 무너지기 때문이다(설계 문서 02절).
    // 실제 전달은 틱 경계의 ClrHost::FlushPhysicsEvents가 한 번에 한다.
    void QueueManagedCollision(const Collision& collider, ClrHost::PhysicsEventKind kind)
    {
        if (nullptr == collider.thisObj) return;

        auto& clr = ClrHost::Get();
        if (!clr.IsReady()) return;

        for (auto* script : collider.thisObj->GetComponents<ScriptComponent>())
        {
            if (nullptr == script || !script->HasInstance()) continue;

            clr.QueuePhysicsEvent(script->GetInstanceId(), kind,
                collider.otherObj, collider.contactPoints);
        }
    }
}

void Scene::OnTriggerEnter(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerEnter);
}

void Scene::OnTriggerStay(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerStay);
}

void Scene::OnTriggerExit(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerExit);
}

void Scene::OnCollisionEnter(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionEnter);
}

void Scene::OnCollisionStay(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionStay);
}

void Scene::OnCollisionExit(const Collision& collider)
{
    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionExit);
}

void Scene::Update(float deltaSecond)
{
    PROFILE_CPU_BEGIN("PreAllUpdateWorldMatrix");
    AllUpdateWorldMatrix();
    PROFILE_CPU_END();

    // 트랙 C3 — Animator는 가상 Update 오버라이드(암묵 구독)를 버리고 전용
    // 시스템의 조밀 배열로 옮겼다. 자리가 RegistryTick 직후인 근거는 실측이다:
    // Animator를 가진 프리팹 17개 전부에서 루트의 스크립트(ModuleBehavior)가
    // Animator를 가진 자식보다 파일상 먼저 나오고, Prefab::InstantiateRecursive가
    // "자기 컴포넌트 먼저 → 자식 재귀" 순으로 등록하므로 옛 update 리스트에서도
    // 스크립트가 먼저였다. 그 상대 순서를 그대로 보존한다 — 다만 이제는 프리팹
    // 구조와 무관하게 "전 스크립트 → 전 Animator"가 결정론적으로 보장된다.
    PROFILE_CPU_BEGIN("AnimatorSystem");
    AnimatorSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    // 트랙 C3 잔여 — 가상 Update 오버라이드(암묵 구독)를 버리고 전용 시스템의
    // 조밀 배열로 옮긴 컴포넌트들. 이 자리인 근거는 옛 위치의 보존이다:
    // 전부 RegistryTick(UpdateList) 안에서 돌아 **두 번째 AllUpdateWorldMatrix
    // (= UpdateUILayout 재실행)보다 항상 먼저**였다. 특히 SpriteSheet·Text는
    // RectTransform의 월드 rect를 읽으므로 그 창을 벗어나면 한 프레임 낡은 값을 본다.
    PROFILE_CPU_BEGIN("DecalSystem");
    DecalSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("FoliageSystem");
    FoliageSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("UITickSystem");
    UITickSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("SoundSystem");
    SoundSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    // 트랙 C2-0 — 재진입 시험의 순회 중 발화점(Scene.h의 TryFireReentrancyStressMidTraversal
    // 상단 주석 참고). Scene 소유 CameraSystem에 콜백을 주입한다. 이 함수 자체는
    // 무장돼 있지 않으면 bool 하나
    // 읽고 끝나므로 카메라가 없거나 시험이 비무장인 평소 프레임엔 비용이
    // 사실상 0이다.
    PROFILE_CPU_BEGIN("CameraSystem");
    m_cameraSystem.Update(deltaSecond, [this]() { TryFireReentrancyStressMidTraversal("CameraSystem", "Update"); });
    PROFILE_CPU_END();

    // 순회 중 발화가 우선이고, 위 CameraSystem 루프가 이미 소비했다면 여기는
    // no-op이다(g_stressArmed가 발화 즉시 false). 이 폴백이 실제로 뭔가 하는
    // 경우는 CameraSystem::m_cameras가 비어(파괴 스트레스로 카메라가 전부
    // 사라졌거나 Awake 전) 순회 중 지점 자체가 안 돈 프레임뿐이다.
    PROFILE_CPU_BEGIN("UpdateEvent");
    PumpReentrancyStress("Update");
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("LightSystem");
    LightSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("PlayerInputSystem");
    PlayerInputSystems->Update(deltaSecond);
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("LateAllUpdateWorldMatrix");
    AllUpdateWorldMatrix();
    PROFILE_CPU_END();
}

void Scene::YieldNull()
{
    CoroutineManagers->yield_Null();
    CoroutineManagers->yield_WaitForSeconds();
    CoroutineManagers->yield_OtherEvent();
    CoroutineManagers->yield_StartCoroutine();
}

void Scene::LateUpdate(float deltaSecond)
{
    // 이 페이즈엔 순회 중 발화점이 없다 — 폴백뿐이다. SceneManager::GameLogic이
    // 같은 호출 안에서 Update를 항상 LateUpdate보다 먼저 부르므로(SceneManager.cpp),
    // Update가 이미 무장을 다 소비한 뒤라 실제로는 거의 항상 no-op이다 — 그래도
    // 남겨 두는 이유는 비용이 bool 하나뿐이고, 앞으로 Update의 호출 순서가 바뀌는
    // 사고에 대비한 마지막 그물이기 때문이다.
    PumpReentrancyStress("LateUpdate");

    // 트랙 C3 잔여 — LateUpdate를 오버라이드하던 둘. 옛 위치(LateUpdateList 안)와
    // 같은 창(RegistryTick 이후 · UpdateRenderData 이전)을 지킨다.
    SoundSystems->LateUpdate(deltaSecond);
    CharacterControllerSystems->LateUpdate(deltaSecond);

    UpdateRenderData();
}

void Scene::EndFramePass()
{
	// 비소유 AI snapshot이 Entity/Component 주소를 읽는 동안 파괴가 겹치지 않게
	// 실제 구조 변경 전에 future를 회수한다.
	DrainAIUpdate();
    PROFILE_CPU_BEGIN("OnDestroyBroadcast");
    // 이 자리가 프레임 끝의 파괴 지점이다 — 바로 아래에서 DestroyComponents와
    // DestroyEntities가 실제 해제를 하므로, 그 직전이 OnDestroy를 부를 마지막 기회다.
    //
    // 레지스트리 경로는 여기서만 파괴가 일어난다는 것을 불변식으로 쓴다: 순회하는
    // 동안에는 리스트에서 아무것도 빠지지 않으므로 '순회 중인 것이 죽는' 상황이
    // 표현 불가능해진다(R1·R2가 여기서 닫힌다).
    FlushPendingDestroy();
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("DestroyLight");
    DestroyLight();
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("DestroyComponents");
    DestroyComponents();
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("DestroyEntities");
    DestroyEntities();
    PROFILE_CPU_END();
    //여기서 병렬처리
    if(!m_AIFuture.valid())
    {
        float deltaSecond = Time->GetElapsedSeconds();
		if (CameraComponent* camera = m_cameraSystem.GetPrimaryCamera())
		{
			const DirectX::BoundingFrustum cameraFrustum = camera->GetFrustum();
			m_AIFuture = std::async(std::launch::async,
				[deltaSecond, cameraFrustum]
				{
					AIManagers->InternalAIUpdate(deltaSecond, cameraFrustum);
				});
		}
    }
}

void Scene::AllDestroyMark()
{
    for (const auto& obj : m_Entities)
    {
        if (obj && !obj->IsDestroyMark() && !obj->IsDontDestroyOnLoad())
            obj->Destroy();
    }
}

void Scene::ResetSelectedEntity()
{
    m_selectedEntity = nullptr;
    m_selectedEntities.clear();
}

void Scene::AddSelectedEntity(Entity* sceneObject)
{
    if (!sceneObject) return;

    if (std::ranges::find(m_selectedEntities, sceneObject) == m_selectedEntities.end())
    {
        m_selectedEntities.push_back(sceneObject);
        m_selectedEntity = sceneObject;
    }
}

void Scene::RemoveSelectedEntity(Entity* sceneObject)
{
    if (!sceneObject) return;
    auto it = std::ranges::find(m_selectedEntities, sceneObject);
    if (it != m_selectedEntities.end())
    {
        m_selectedEntities.erase(it);
        if (m_selectedEntity == sceneObject)
        {
            m_selectedEntity = m_selectedEntities.empty() ? nullptr : m_selectedEntities.back();
        }
    }
}

void Scene::ClearSelectedEntities()
{
    m_selectedEntities.clear();
    m_selectedEntity = nullptr;
}

void Scene::CollectLightComponent(LightComponent* ptr)
{
    if (ptr) push_unique(m_lightComponents, ptr);
}

void Scene::UnCollectLightComponent(LightComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_lightComponents, [ptr](const auto& light) { return light == ptr; });
    }
}

// UpdateLight가 여기 있었다 — 옛 m_lights 전체를 매 프레임 훑어 LightProperties
// 배열로 복사했고, 그것이 렌더러가 광원을 보는 유일한 통로였다. 광원이
// 등록/해제 기반 프록시(LightRenderProxy)로 옮겨 가면서 소비자가 사라졌다.
//
// 아래 남은 것은 편집기 부기다: m_lightIndex(기즈모가 "메인 라이트"를 가리는
// 데 쓴다)를 발급하고, DestroyLight가 점유 슬롯을 압축하며 그 인덱스를
// 다시 맞춘다. 그리는 값은 더 이상 여기를 지나지 않는다.

size_t Scene::AddLight()
{
    m_lightSlots.push_back(1u);
    return m_lightSlots.size() - 1u;
}

void Scene::EnsureLightSlot(size_t index)
{
    // 로드 경로가 여기로 온다. LightComponent::m_lightIndex는 직렬화되므로
    // 되살아난 컴포넌트는 AddLight()가 아니라 이 함수로 **이미 있어야 할 슬롯**을
    // 집는데, 갓 로드된 Scene의 m_lightSlots는 비어 있다. 슬롯을 자라게 하는 책임이
    // 전부 여기에 있다.
    //
    // 조건이 `index > size()`였다. 라이트가 하나면 뒤에 붙어 있던
    // `|| 0 == m_lightSlots.size()`가 우연히 구해 줘서(index 0 → resize(1)) 살아났고,
    // 둘째부터 `1 > 1` 거짓 · `0 == 1` 거짓으로 resize가 돌지 않아 두 번째 슬롯이
    // 범위 밖이 됐다 — vector 첨자 초과(0xC0000409). 그 뒤 조건이 off-by-one을
    // 가리는 역할을 해, 라이트 하나짜리 씬만 열어 보는 동안에는 보이지 않았다.
    // (verify-light-slot-restore.ps1이 라이트를 셋 두는 이유)
    if (index >= m_lightSlots.size())
    {
        m_lightSlots.resize(index + 1u, 0u);
    }
    m_lightSlots[index] = 1u;
}

void Scene::RemoveLight(size_t index)
{
    if (index < m_lightSlots.size())
    {
        m_lightSlots[index] = 0u;
    }
}

void Scene::DestroyLight()
{
    std::unordered_map<size_t, size_t> indexRemap;
    std::vector<uint8_t> newLightSlots;
    bool isFirstDirectional = false;

    newLightSlots.reserve(m_lightSlots.size());

    for (size_t i = 0; i < m_lightSlots.size(); ++i)
    {
        if (0u != m_lightSlots[i])
        {
            indexRemap[i] = newLightSlots.size();
            newLightSlots.push_back(1u);
        }
    }

    m_lightSlots = std::move(newLightSlots);

    for (auto& comp : m_lightComponents)
    {
        if (!comp) continue;

        int& lightIndex = comp->m_lightIndex;
        auto& lightType = comp->m_lightType;
        if (auto it = indexRemap.find(lightIndex); it != indexRemap.end())
        {
            lightIndex = static_cast<int>(it->second);
        }

        if (!isFirstDirectional && lightType == LightType::DirectionalLight)
        {
            isFirstDirectional = true;
            comp->m_lightStatus = LightStatus::StaticShadows;
        }
    }
}

void Scene::CollectMeshRenderer(MeshRenderer* ptr)
{
    if (ptr)
    {
        m_allMeshRenderers.push_back(ptr);
        if (ptr->IsSkinnedMesh())
        {
            m_skinnedMeshRenderers.push_back(ptr);
        }
        else
        {
            m_staticMeshRenderers.push_back(ptr);
        }
    }
}

void Scene::UnCollectMeshRenderer(MeshRenderer* ptr)
{
    if (ptr)
    {
        if (ptr->IsSkinnedMesh())
        {
            std::erase_if(m_skinnedMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
        }
        else
        {
            std::erase_if(m_staticMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
        }
        std::erase_if(m_allMeshRenderers, [ptr](const auto& mesh) { return mesh == ptr; });
    }
}

void Scene::CollectSpriteRenderer(SpriteRenderer* ptr)
{
    if (ptr)
    {
        m_spriteRenderers.push_back(ptr);
    }
}

void Scene::UnCollectSpriteRenderer(SpriteRenderer* ptr)
{
    if (ptr)
    {
        std::erase_if(m_spriteRenderers, [ptr](const auto& sprite) { return sprite == ptr; });
    }
}

void Scene::CollectTerrainComponent(TerrainComponent* ptr)
{
    if (ptr)
    {
        m_terrainComponents.push_back(ptr);
    }
}

void Scene::UnCollectTerrainComponent(TerrainComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_terrainComponents, [ptr](const auto& mesh) { return mesh == ptr; });
    }
}

void Scene::CollectFoliageComponent(FoliageComponent* ptr)
{
    if (ptr)
    {
        m_foliageComponents.push_back(ptr);
    }
}

void Scene::UnCollectFoliageComponent(FoliageComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_foliageComponents, [ptr](const auto& comp) { return comp == ptr; });
    }
}

void Scene::CollectDecalComponent(DecalComponent* ptr)
{
    if (ptr)
    {
        m_decalComponents.push_back(ptr);
    }
}

void Scene::UnCollectDecalComponent(DecalComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_decalComponents, [ptr](const auto& comp) { return comp == ptr; });
    }
}

void Scene::CollectRigidBodyComponent(RigidBodyComponent* ptr)
{
    if (ptr) push_unique(m_rigidBodyComponents, ptr);
}

void Scene::UnCollectRigidBodyComponent(RigidBodyComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_rigidBodyComponents, [ptr](const auto& body) { return body == ptr; });
    }
}

void Scene::CollectColliderComponent(BoxColliderComponent* ptr)
{
    if (ptr)
    {
        push_unique(m_boxColliderComponents, ptr);

        PhysicsManagers->AddCollider(ptr);

        auto callback = [=](const EBodyType& bodyType)
        {
            if (nullptr == ptr) return;

            auto boxInfo = ptr->GetBoxInfo();
            auto colliderID = boxInfo.colliderInfo.id;

            if (bodyType == EBodyType::STATIC)
            {
                Physics->CreateStaticBody(boxInfo, ptr->GetColliderType());
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{ m_boxTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
            else
            {
                bool isKinematic = bodyType == EBodyType::KINEMATIC;
                Physics->CreateDynamicBody(boxInfo, ptr->GetColliderType(), isKinematic);
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{
                        m_boxTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
        };

        m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
    }
}

void Scene::UnCollectColliderComponent(BoxColliderComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_boxColliderComponents, [ptr](const auto& box) { return box == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::CollectColliderComponent(SphereColliderComponent* ptr)
{
    if (ptr)
    {
        push_unique(m_sphereColliderComponents, ptr);

        PhysicsManagers->AddCollider(ptr);

        auto callback = [=](const EBodyType& bodyType)
        {
            if (nullptr == ptr) return;

            auto sphereInfo = ptr->GetSphereInfo();
            auto colliderID = sphereInfo.colliderInfo.id;

            if (bodyType == EBodyType::STATIC)
            {
                Physics->CreateStaticBody(sphereInfo, ptr->GetColliderType());
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{ m_sphereTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
            else
            {
                bool isKinematic = bodyType == EBodyType::KINEMATIC;
                Physics->CreateDynamicBody(sphereInfo, ptr->GetColliderType(), isKinematic);
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{
                        m_sphereTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
        };

        m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
    }
}

void Scene::UnCollectColliderComponent(SphereColliderComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_sphereColliderComponents, [ptr](const auto& sphere) { return sphere == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::CollectColliderComponent(CapsuleColliderComponent* ptr)
{
    if (ptr)
    {
        push_unique(m_capsuleColliderComponents, ptr);

        PhysicsManagers->AddCollider(ptr);

        auto callback = [=](const EBodyType& bodyType)
        {
            if (nullptr == ptr) return;

            auto capsuleInfo = ptr->GetCapsuleInfo();
            auto colliderID = capsuleInfo.colliderInfo.id;

            if (bodyType == EBodyType::STATIC)
            {
                Physics->CreateStaticBody(capsuleInfo, ptr->GetColliderType());
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{ m_capsuleTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
            else
            {
                bool isKinematic = bodyType == EBodyType::KINEMATIC;
                Physics->CreateDynamicBody(capsuleInfo, ptr->GetColliderType(), isKinematic);
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{
                        m_capsuleTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
        };

        m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
    }
}

void Scene::UnCollectColliderComponent(CapsuleColliderComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_capsuleColliderComponents, [ptr](const auto& capsule) { return capsule == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::CollectColliderComponent(MeshColliderComponent* ptr)
{
    if (ptr)
    {
        push_unique(m_meshColliderComponents, ptr);

        PhysicsManagers->AddCollider(ptr);

        auto callback = [=](const EBodyType& bodyType)
        {
            if (nullptr == ptr) return;

            auto convexMeshInfo = ptr->GetMeshInfo();
            auto colliderID = convexMeshInfo.colliderInfo.id;

            if (bodyType == EBodyType::STATIC)
            {
                Physics->CreateStaticBody(convexMeshInfo, ptr->GetColliderType());
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{ m_boxTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
            else
            {
                bool isKinematic = bodyType == EBodyType::KINEMATIC;
                Physics->CreateDynamicBody(convexMeshInfo, ptr->GetColliderType(), isKinematic);
                m_colliderContainer[colliderID] =
                    PhysicsManager::ColliderInfo{
                        m_boxTypeId,
                        ptr,
                        ptr->GetOwner(),
                        ptr,
                        false
                };
            }
        };

        m_ColliderTypeLinkCallback.insert({ ptr->GetOwner(), std::move(callback) });
    }
}

void Scene::UnCollectColliderComponent(MeshColliderComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_meshColliderComponents, [ptr](const auto& mesh) { return mesh == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::CollectColliderComponent(CharacterControllerComponent* ptr)
{
    if (ptr)
    {
        push_unique(m_characterControllerComponents, ptr);

        PhysicsManagers->AddCollider(ptr);

        auto controllerInfo = ptr->GetControllerInfo();
        auto colliderID = controllerInfo.id;

        m_colliderContainer[colliderID] =
            PhysicsManager::ColliderInfo{ m_controllerTypeId,
                ptr,
                ptr->GetOwner(),
                ptr,
                false
        };
    }
}

void Scene::CollectColliderComponent(TerrainColliderComponent* ptr)
{
    if (ptr)
    {
        PhysicsManagers->AddCollider(ptr);

        auto gameObject = ptr->GetOwner();
        auto heightFieldInfo = ptr->GetHeightFieldColliderInfo();
        auto colliderID = heightFieldInfo.colliderInfo.id;

        m_colliderContainer.insert({ colliderID, {
            m_heightFieldTypeId,
            ptr,
            gameObject,
            ptr,
            false
        } });
    }
}

void Scene::UnCollectColliderComponent(CharacterControllerComponent* ptr)
{
    if (ptr)
    {
        std::erase_if(m_characterControllerComponents, [ptr](const auto& character) { return character == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::UnCollectColliderComponent(TerrainColliderComponent* ptr)
{
    if (ptr)
    {
        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::DestroyEntities()
{
    std::unordered_set<uint32_t> deletedIndices;
    for (const auto& obj : m_Entities)
    {
        if (obj && obj->IsDestroyMark())
            deletedIndices.insert(obj->m_index);
    }

    if (deletedIndices.empty())
        return;

    // 슬롯 tombstone(reset)+free 리스트+세대 증가로 대체한다(트랙 E1) — 예전에는
    // 여기서 압축 후 생존자 전원의 인덱스를 재부여했다(N-6). 그 재부여 루프에는
    // m_rootIndex를 unordered_map::operator[]로 무가드 조회하는 결함도 있었는데
    // (조회 실패 시 0을 조용히 끼워 넣는다), 재부여 자체가 없어지며 함께 없어진다.
    // 생존자의 인덱스는 이 루프가 끝난 뒤에도 절대 바뀌지 않는다.
    for (uint32_t index : deletedIndices)
    {
        // 루트(0)는 절대 해제하지 않는다 — AllDestroyMark 등이 루트까지 마크해도
        // 여기서 막힌다.
        if (0 == index) continue;
        if (index >= m_Entities.size()) continue;

        auto& obj = m_Entities[index];
        if (!obj) continue;

        // 자식들의 부모 링크를 끊는다. 자식도 함께 파괴 대상이면 곧 자신의
        // 차례에 tombstone되므로, 여기서는 생존 자식에게만 실질적인 효과가 있다.
        for (auto childIdx : obj->GetChildrenIndices())
        {
            if (Entity::IsValidIndex(childIdx) &&
                static_cast<size_t>(childIdx) < m_Entities.size() &&
                m_Entities[childIdx])
            {
                m_Entities[childIdx]->SetParentIndex(Entity::INVALID_INDEX);
            }
        }
        obj->ClearChildren();

        // 부모(또는 씬 루트)의 children 목록에서 자신을 뗀다 — 안 하면 죽은
        // 인덱스가 남아, 슬롯이 재사용됐을 때 엉뚱한 객체를 가리킨다.
        UnlinkFromParentChildren(static_cast<Entity::Index>(index));

        ReleaseSlot(static_cast<Entity::Index>(index));
    }
}

void Scene::DestroyComponents()
{
    for (auto& obj : m_Entities)
    {
        if (obj)
        {
            bool isDirty = false;
            for (auto& component : obj->m_components)
            {
                if (!component || !component->IsDestroyMark() || component->IsDontDestroyOnLoad())
                {
                    continue;
                }
                isDirty = true;

                obj->RemoveComponentTypeID(component->GetTypeID());

                component.reset();
            }

            if (false == isDirty) continue;
            std::erase_if(obj->m_components, [](const auto& component)
                {
                    return component == nullptr;
                });
            obj->RefreshComponentIdIndices();
        }
    }
}

std::string Scene::GenerateUniqueEntityName(const std::string_view& name)
{
    std::string baseName{ name };
    std::string uniqueName{ name };

    // Remove trailing numeric suffix like " (1)" if present
    const auto lparen = baseName.find_last_of('(');
    const auto rparen = baseName.find_last_of(')');
    if (lparen != std::string::npos && rparen == baseName.length() - 1 && lparen < rparen)
    {
        const std::string_view numberPart{ baseName.data() + lparen + 1, rparen - lparen - 1 };
        if (!numberPart.empty() && baseName[lparen - 1] == ' ' &&
            std::ranges::all_of(numberPart, [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
        {
            baseName = baseName.substr(0, lparen - 1);
            uniqueName = baseName;
        }
    }

    int count = 1;
    while (m_entityNameSet.contains(uniqueName))
    {
        uniqueName = baseName + " (" + std::to_string(count++) + ")";
    }
    m_entityNameSet.insert(uniqueName);
    return uniqueName;
}

void Scene::RemoveEntityName(const std::string_view& name)
{
    m_entityNameSet.erase(name.data());
}

// 순회 진입 가드 단일화 구현(선언은 Scene.h — 이유·수렴 안 시킨 두 곳의 근거도
// 거기 있다). Debug 전역이 필요해 Scene.h가 아니라 여기서 정의하고, 실제로
// 쓰이는 두 키 타입(Entity::Index·GameObject*)만 명시 인스턴스화한다 —
// 이 TU 밖에서는 못 쓴다는 뜻이고, Scene.h에 인라인으로 두면 Debug 전역을
// include하지 않은 다른 TU에서 컴파일이 깨질 위험이 있다(유니티 빌드).
template<typename Key>
bool Scene::TryEnterTraversal(std::unordered_set<Key>& visited, const Key& key,
    int depth, const char* traversalLabel, std::string_view nodeName)
{
    if (!visited.insert(key).second) return false;

    if (depth > kTraversalMaxDepth)
    {
        // 왜 static bool이 위험했나 — 이 인스턴스화(Key=GameObject::Index, 즉
        // TryEnterTraversal<Entity::Index>)는 UpdateModelRecursive를 거쳐
        // AllUpdateWorldMatrix의 std::for_each(std::execution::par, ...)(아래,
        // Scene.cpp)에서 루트 자식마다 별도 스레드로 불린다. C++11 매직 스태틱은
        // "최초 생성"만 스레드 안전을 보장하고, 그 이후의 평범한 bool 읽기/쓰기는
        // 전혀 동기화되지 않는다 — 서로 다른 루트 브랜치 두 곳이 동시에 첫
        // 깊이초과를 겪으면 이 read-modify-write가 데이터 레이스(표준상 UB, TSan
        // 적중)였다.
        // 왜 원자적 exchange여야 하나 — 타입만 std::atomic<bool>로 바꾸고
        // "검사 후 대입"(if (!reported) reported = true;) 형태를 그대로 두면,
        // 두 스레드가 모두 대입 전에 !reported를 관측하는 창이 여전히 남아
        // 로그가 2회 이상 찍힐 수 있다. exchange(true)의 반환값(대입 직전의
        // 이전 값)으로 "내가 최초 통과자인가"를 원자적으로 판정해야 정확히
        // 1회만 통과한다.
        // 참고 — TryEnterTraversal<Entity*>(UI 레이아웃, LayoutUINode →
        // UpdateUILayout)는 직렬 호출이라 애초에 레이스가 없었다. 하지만 템플릿
        // 본문은 두 인스턴스화가 공유하므로 이 수정도 함께 적용된다.
        static std::atomic<bool> reported{ false };
        if (!reported.exchange(true, std::memory_order_relaxed))
        {
            Debug->LogError(std::string(traversalLabel)
                + "가 최대 깊이를 넘었다 — 계층이 지나치게 깊거나 순환한다: "
                + std::string(nodeName));
        }
        return false;
    }
    return true;
}
template bool Scene::TryEnterTraversal<Entity::Index>(
    std::unordered_set<Entity::Index>&, const Entity::Index&, int, const char*, std::string_view);
template bool Scene::TryEnterTraversal<Entity*>(
    std::unordered_set<Entity*>&, Entity* const&, int, const char*, std::string_view);

void Scene::UpdateModelRecursive(Entity::Index objIndex, math::matrix4x4 model, bool parentChanged,
    std::unordered_set<Entity::Index>* visited, int depth)
{
    if (objIndex == Entity::INVALID_INDEX || objIndex < 0 ||
        static_cast<size_t>(objIndex) >= m_Entities.size())
    {
        return;
    }

    const auto& obj = m_Entities[objIndex];


    if (!obj || obj->IsDestroyMark())
    {
        return;
    }

    // AllUpdateWorldMatrix가 루트 자식 단위로 std::execution::par 병렬 실행하므로
    // 방문집합을 공유하면 레이스가 난다 — 최초 호출(visited==nullptr)에서만 이
    // 스택 프레임에 만들어 재귀 내내 포인터로 물려준다. optional인 이유: MSVC의
    // unordered_set은 기본 생성자가 버킷을 즉시 할당해, 재귀 호출마다 만들면
    // 핫패스에 노드당 할당이 얹힌다. 계층에 순환이 있어도 여기서 멈춘다.
    std::optional<std::unordered_set<Entity::Index>> localVisited;
    if (!visited)
    {
        localVisited.emplace();
        visited = &*localVisited;
    }
    if (!TryEnterTraversal(*visited, objIndex, depth, "[Transform] 월드 행렬 갱신 순회", obj->m_name.ToString()))
    {
        return;
    }

    // S2(dirty push / lazy pull) — 자식에게 물려줄 "이번 순회에서 바뀌었다"
    // 신호. UI 분기는 자기 몫의 트랜스폼이 없으므로 받은 값을 그대로 물려주고,
    // Bone과 default의 재계산 경로만 true로 올린다. default의 스킵 경로는
    // parentChanged를 받은 그대로(false) 둔다 — 이 노드도 부모도 안 바뀌었으니
    // 자식에게 강제할 이유가 없다(자식은 각자 자기 dirty를 스스로 본다).
    bool childParentChanged = parentChanged;

    // ★ E7 — 공간 데이터가 없으면(S3의 UI) 트랜스폼 갱신 자체가 성립하지 않는다.
    // 예전에는 저장된 GameObjectType::UI로 갈랐지만, 이제 "Transform을 갖는가"가
    // 정본이다 — 같은 것을 두 곳에 적어 두지 않는다. Canvas는 rect와 Transform을
    // 둘 다 가지므로 여기 안 걸리고 아래 default로 간다(월드 공간 캔버스 보존).
    if (!obj->HasTransform())
    {
        for (auto childIndex : obj->GetChildrenIndices())
        {
            if (childIndex == obj->m_index) continue;
            UpdateModelRecursive(childIndex, model, parentChanged, visited, depth + 1);
        }
        return;
    }

	// ★ E7-b/E7-c(트랙 E) — Bone 판정은 BoneComponent 보유가 정본이다.
	// 나머지 공간 판정도 위 HasTransform()으로 끝나므로 저장 타입 switch는 없다.
    if (BoneComponent* boneComp = obj->GetComponent<BoneComponent>())
    {
        const auto& rootObj = TryGetEntity(obj->GetRootIndex());
        if (!rootObj)
        {
            return;
        }
        const auto& animator = rootObj->GetComponent<Animator>();
        if (!animator || !animator->m_Skeleton || !animator->IsEnabled())
        {
            return;
        }

        // 캐시 갱신 — m_resolvedSerial이 지금 애니메이터의 스켈레톤 일련번호와
        // 다르거나(아직 못 풀었음·모델을 갈아 끼워 스켈레톤이 바뀜) m_boneIndex가
        // 무효(-1, 이전 탐색 실패)면 그때만 FindBone(문자열 선형 탐색)을 다시
        // 돈다. ★ 늦은 로드 허용 — 스켈레톤이 이번 프레임에 처음 붙었으면
        // m_resolvedSerial(이전 값, 0이거나 다른 번호)과 자동으로 어긋나므로
        // 여기서 다시 풀린다. 옛 코드가 매 프레임 FindBone을 공짜로 다시 돌던
        // 것과 관측 가능한 차이가 없다 — 스켈레톤이 안 바뀐 프레임만 캐시를 쓴다.
        //
        // 포인터가 아니라 일련번호로 비교하는 이유는 Skeleton::m_serial 주석
        // 참고(해제된 주소가 재할당되면 포인터 비교는 거짓 적중한다).
        Skeleton* skeleton = animator->m_Skeleton;
        if (!IsBoneCacheEnabled() || boneComp->m_resolvedSerial != skeleton->m_serial || boneComp->m_boneIndex < 0)
        {
            Bone* const bone = skeleton->FindBone(obj->RemoveSuffixNumberTag());
            boneComp->m_boneIndex = bone ? bone->m_index : -1;
            boneComp->m_resolvedSerial = skeleton->m_serial;
        }

        // ★ 범위 검사 — m_localTransforms는 크기 고정 배열(MAX_BONES=512,
        // Animator.h)이다. 위 m_resolvedFor 비교가 "다른 스켈레톤"은 이미
        // 걸러내지만, 캐시에 담긴 인덱스를 실제로 쓰기 전에 배열 경계를 한 번
        // 더 확인한다 — 인덱스가 파생값이라 저장하지 않기로 한 것과 같은 이유
        // (BoneComponent.h 주석)로, 쓰는 자리에서 스스로를 방어한다.
        const bool hasValidIndex = boneComp->m_boneIndex >= 0
            && static_cast<size_t>(boneComp->m_boneIndex) < std::size(animator->m_localTransforms);

        const math::matrix4x4 local = hasValidIndex
            ? animator->m_localTransforms[boneComp->m_boneIndex]
            : obj->Transform_().GetLocalMatrix();
        obj->Transform_().SetAndDecomposeMatrix(local * model);
        // 애니메이션이 매 프레임 로컬 행렬을 갈아치우므로 dirty 플래그에 기대지
        // 않고 항상 재계산·전파한다(S2 범위 밖 — C3가 애니메이션 자체는 손댄다).
        childParentChanged = true;
    }
	else
    {
        // dirty 인지 순회의 본체. mustRecompute 네 조건 중 하나라도 참이면
        // 기존과 동일하게 GetLocalMatrix+곱셈+SetAndDecomposeMatrix를 전부
        // 수행한다 — 토글 꺼짐은 옛(항상 재계산) 동작과 바이트 단위로 같다.
        //
        // worldChangedExternally: dirty(로컬 포즈 재계산 플래그)와 독립인 신호 —
        // ClrHost::EnsureWorldMatrix처럼 이 순회 밖에서 조상 체인만 앞당겨
        // 갱신하는 호출이 dirty를 먼저 꺼버려도, SetAndDecomposeMatrix가 실제로
        // 값을 쓴 이 흔적은 남는다(TransformStore.h worldChanged 주석). 이걸 안
        // 보면 그런 호출 뒤에 이 노드의 "정상" 형제 서브트리가 갱신을 놓친다.
        // ★ 게이트는 스토어를 슬롯으로 직접 읽는다 — 실측 근거.
        //
        // Transform의 접근자는 호출마다 ResolveStore()를 돈다(소유자→씬→
        // GetEntityRaw로 "이 슬롯의 진짜 점유자가 나인가" 확인 →
        // GetTransformStore). 게이트가 그걸 노드마다 두 번(dirty·worldChanged)
        // 물면, 아껴 낸 decompose보다 재해석이 더 비싸진다 — Release 실측에서
        // 10,000개·10% 이동 시나리오가 옛 경로보다 약 4% **느렸다**. 이 순회는
        // 바로 위에서 m_Entities[objIndex]로 obj를 꺼냈으므로 점유자 확인이
        // 이미 끝나 있다(그게 ResolveStore가 하는 검사 그 자체다). 슬롯 = objIndex.
        // Transform.h StoreSlot 주석이 "트래버설 경로의 캐시(재해석 생략)는 S2
        // 소관"이라고 미리 적어 둔 자리가 여기다.
        const size_t storeSlot = static_cast<size_t>(objIndex);
        const bool hasStoreSlot = storeSlot < m_transformStore.Size();

        bool worldChangedExternally = false;
        bool localDirty = false;
        if (hasStoreSlot)
        {
            worldChangedExternally = (0 != m_transformStore.worldChanged[storeSlot]);
            m_transformStore.worldChanged[storeSlot] = 0;   // ConsumeWorldChanged와 같은 의미(읽고 내린다)
            localDirty = (0 != m_transformStore.dirty[storeSlot]);
        }
        else
        {
            // 스토어에 못 붙은 오브젝트(로컬 폴백 경로) — 드물다. 접근자로 간다.
            worldChangedExternally = obj->Transform_().ConsumeWorldChanged();
            localDirty = obj->Transform_().IsDirty();
        }

        const bool mustRecompute = !IsDirtyTraversalEnabled() || parentChanged
            || localDirty || worldChangedExternally;

        if (!mustRecompute)
        {
            // 이 노드도 부모도 안 바뀌었다 — 월드 행렬이 지난 순회와 같다고
            // 보장된다. fetch·곱셈·decompose를 통째로 건너뛴다. 다만 자식이
            // 개별적으로 dirty일 수 있으므로 순회 자체(아래 for)는 계속하고,
            // 그때 넘길 "부모의 월드"는 인자로 받은 model(조상에서 온 값)이
            // 아니라 이 노드에 이미 저장된(안 바뀐) 월드 행렬이어야 한다.
            // 스킵 경로가 이 슬라이스에서 가장 자주 도는 자리라, 여기도 접근자
            // 대신 슬롯 직독으로 간다(위 게이트와 같은 근거).
            model = hasStoreSlot ? m_transformStore.worldMatrix[storeSlot]
                                 : obj->Transform_().GetWorldMatrix();
        }
		else
		{
			if (localDirty)
			{
				auto renderer = obj->GetComponent<MeshRenderer>();
				if (renderer)
				{
					renderer->SetNeedUpdateCulling(true);
				}
			}
			model = obj->Transform_().GetLocalMatrix() * model;
			obj->Transform_().SetAndDecomposeMatrix(model);
			childParentChanged = true;
		}
    }

    for (auto childIndex : obj->GetChildrenIndices())
    {
        if (childIndex == obj->m_index) continue;
        UpdateModelRecursive(childIndex, model, childParentChanged, visited, depth + 1);
    }
}

void Scene::LayoutUINode(Entity* obj, const math::rect& parentRect,
    float parentScale, bool parentChanged, bool isTopLevel, int depth,
    std::unordered_set<Entity*>& visited)
{
    if (nullptr == obj || obj->IsDestroyMark()) return;

    // 이미 계산한 노드는 건드리지 않는다. 두 번째 방문은 부모 문맥이 달라서
    // 배율을 1로 덮어쓰고 캔버스 rect를 앵커로 다시 계산해 버린다.
    // (1920x1080에서는 배율이 마침 1이라 증상이 없어, 해상도를 바꿔야만 드러난다.)
    // 계층에 순환이 있어도, 깊이가 비정상적으로 깊어져도 여기서 멈춘다
    // (TryEnterTraversal — UpdateModelRecursive와 공유하는 가드, Scene.h 참고).
    if (!TryEnterTraversal(visited, obj, depth, "[UI] 레이아웃 순회", obj->m_name.ToString()))
    {
        return;
    }

    math::rect childRect = parentRect;
    float childScale = parentScale;
    bool childChanged = parentChanged;
    bool childIsTopLevel = false;

    auto* rect = obj->GetComponent<RectTransformComponent>();
    Canvas* canvas = obj->GetComponent<Canvas>();

    if (nullptr != rect && isTopLevel && nullptr != canvas)
    {
        // 최상위 캔버스는 앵커로 계산되는 대상이 아니라 화면이 값을 정해 주는 노드다(7-1).
        // 중첩 캔버스(부모가 rect를 정해 주는 경우)는 아래 일반 경로로 간다 — uGUI도
        // 중첩 캔버스의 스케일러는 무시하고 루트 배율을 물려준다.
        const math::rect screenRect = RectTransformComponent::GetScreenRootRect();
        if (CanvasRenderMode::WorldSpace == canvas->GetRenderMode())
        {
            childScale = 1.f;
            childChanged = rect->DriveAsWorldCanvasRoot();
        }
        else
        {
            childScale = canvas->ComputeScaleFactor(screenRect);
            childChanged = rect->DriveAsCanvasRoot(screenRect, childScale);
        }
        childRect = rect->GetWorldRect();
    }
    else if (nullptr != rect)
    {
        // 활성 여부는 보지 않는다. 예전 전파 경로가 그랬고, 그래야 맞다 —
        // 꺼져 있던 UI를 켜는 순간 rect가 0인 채로 나타나면 안 된다.
        // (7-5에서 IsEnabled 검사를 넣었다가 무기 슬롯 하위 128개가 통째로
        //  0,0,0,0으로 무너졌다. 그쪽이 평소 비활성인 계층이었다.)
        //
        // 부모 rect가 변했으면 자식도 다시 계산한다 — dirty 규칙은 여기 한 줄이다(F-10).
        if (parentChanged) rect->MarkDirty();
        rect->SetLayoutScale(parentScale);

        childChanged = rect->UpdateLayout(parentRect);
        childRect = rect->GetWorldRect();
    }
    else
    {
        // RectTransform이 없는 노드는 UI 좌표계에 관여하지 않는다. 그 아래에 UI가
        // 있으면 최상위로 취급한다 — ResolveParentRect가 쓰던 규칙과 같다.
        childRect = RectTransformComponent::GetScreenRootRect();
        childScale = 1.f;
        childChanged = false;
        childIsTopLevel = true;
    }

    for (auto childIndex : obj->GetChildrenIndices())
    {
        if (childIndex == obj->m_index) continue;
        LayoutUINode(TryGetEntity(childIndex), childRect, childScale,
            childChanged, childIsTopLevel, depth + 1, visited);
    }
}

void Scene::UpdateUILayout()
{
    if (m_Entities.empty()) return;

    const math::rect screenRect = RectTransformComponent::GetScreenRootRect();
    std::unordered_set<Entity*> visited;

    // 씬 루트의 자식부터 한 번만 훑는다. 캔버스 구동도, 캔버스 밑에 없는 UI도
    // 전부 이 한 순회 안에서 처리된다.
    for (auto rootIndex : m_Entities[0]->GetChildrenIndices())
    {
        LayoutUINode(TryGetEntity(rootIndex), screenRect, 1.f,
            /*parentChanged=*/false, /*isTopLevel=*/true, 0, visited);
    }

    // 루트에서 닿지 않는 캔버스가 있으면 여기서 줍는다. 예전 구현이 Canvases 목록을
    // 따로 돌았으므로, 그런 캔버스가 있어도 동작이 달라지지 않게 남겨 둔다.
    for (const auto& canvasHandle : Canvases)
    {
        // 해석 실패는 정상 경로다 — 캐시가 낡았다는 뜻이고, 그때는 건너뛰는 것이
        // 옳다(Scene.h의 Canvases 주석). 여기서 지우지는 않는다: UIManager::SortCanvas가
        // 청소 담당이고, 순회 중 캐시를 줄이면 이 루프가 원소를 건너뛴다.
        Entity* canvasObj = Resolve(canvasHandle);
        if (!canvasObj || canvasObj->IsDestroyMark()) continue;
        if (visited.count(canvasObj)) continue;

        LayoutUINode(canvasObj, screenRect, 1.f, false, true, 0, visited);
    }
}

void Scene::LayoutUISubtree(Entity* root)
{
    if (nullptr == root) return;

    // 부모 기준은 컴포넌트와 같은 규칙으로 찾는다 — 여기에 (0,0,W,H)를 따로 적어
    // 두었던 곳들이 캔버스 규약과 어긋나 있었다(PHASE 7-2에서 두 곳, 7-5에서 세 곳).
    math::rect parentRect = RectTransformComponent::GetScreenRootRect();
    float parentScale = 1.f;
    bool isTopLevel = true;

    const Entity::Index parentIndex = root->GetParentIndex();
    if (Entity::IsValidIndex(parentIndex))
    {
        if (Entity* parent = TryGetEntity(parentIndex))
        {
            if (auto* parentRT = parent->GetComponent<RectTransformComponent>())
            {
                parentRect = parentRT->GetWorldRect();
                parentScale = parentRT->GetLayoutScale();
                isTopLevel = false;
            }
        }
    }

    std::unordered_set<Entity*> visited;
    LayoutUINode(root, parentRect, parentScale, /*parentChanged=*/true, isTopLevel, 0, visited);
}

void Scene::SetInternalPhysicData()
{
    if (!m_colliderContainer.empty())
    {
        std::erase_if(m_colliderContainer,
        [&](const auto& pair)
        {
            return pair.second.bIsDestroyed == true;
        });
    }

    std::unordered_map<Entity*, EBodyType> m_bodyType;

    for (auto& rigid : m_rigidBodyComponents)
    {
        auto gameObject = rigid->GetOwner();
        m_bodyType[gameObject] = rigid->GetBodyType();
    }

    std::unordered_set<Entity*> linkCompleteSet;
    for (auto& box : m_boxColliderComponents)
    {
        if (box && box->GetOwner())
        {
            auto gameObject = box->GetOwner();
            auto iter = m_ColliderTypeLinkCallback.find(gameObject);
            if (iter != m_ColliderTypeLinkCallback.end())
            {
                iter->second(m_bodyType[gameObject]);
            }
            linkCompleteSet.insert(gameObject);
        }
    }

    for (auto& sphere : m_sphereColliderComponents)
    {
        if (sphere && sphere->GetOwner())
        {
            auto gameObject = sphere->GetOwner();
            auto iter = m_ColliderTypeLinkCallback.find(gameObject);
            if (iter != m_ColliderTypeLinkCallback.end())
            {
                iter->second(m_bodyType[gameObject]);
            }
            linkCompleteSet.insert(gameObject);
        }
    }

    for (auto& capsule : m_capsuleColliderComponents)
    {
        if (capsule && capsule->GetOwner())
        {
            auto gameObject = capsule->GetOwner();
            auto iter = m_ColliderTypeLinkCallback.find(gameObject);
            if (iter != m_ColliderTypeLinkCallback.end())
            {
                iter->second(m_bodyType[gameObject]);
            }
            linkCompleteSet.insert(gameObject);
        }
    }

    for (auto& mesh : m_meshColliderComponents)
    {
        if (mesh && mesh->GetOwner())
        {
            auto gameObject = mesh->GetOwner();
            auto iter = m_ColliderTypeLinkCallback.find(gameObject);
            if (iter != m_ColliderTypeLinkCallback.end())
            {
                iter->second(m_bodyType[gameObject]);
            }
            linkCompleteSet.insert(gameObject);
        }
    }

    if (!m_ColliderTypeLinkCallback.empty())
    {
        std::erase_if(m_ColliderTypeLinkCallback,
            [&linkCompleteSet](const auto& pair)
            {
                return linkCompleteSet.contains(pair.first);
            });
    }
}

void Scene::AllUpdateWorldMatrix()
{
    // UI 레이아웃은 부모→자식 의존 사슬이라 직렬로 먼저 끝낸다. 아래 병렬 순회는
    // 트랜스폼 행렬만 다루므로 UI rect를 건드리지 않는다(PHASE 7-5).
    UpdateUILayout();

    if (m_Entities.empty()) return;

    const auto& rootObjects = m_Entities[0]->GetChildrenIndices();

    auto updateFunc = [this](Entity::Index index)
    {
		UpdateModelRecursive(index, math::matrix4x4::identity());
    };

    if (!rootObjects.empty())
    {
        std::for_each(std::execution::par, rootObjects.begin(), rootObjects.end(), updateFunc);
    }
}

void Scene::AllUIUpdateWorldMatrix()
{
    // 일시정지 중 UI만 갱신하는 경로. 이제 UpdateUILayout 하나면 된다(PHASE 7-5).
    UpdateUILayout();
}

void Scene::AddCanvas(Entity* canvas)
{
    if (!canvas) return;

    // 이 씬 소속이 아니면 담지 않는다. 담아도 Resolve가 sceneId에서 거르므로
    // 처음부터 죽은 항목이 될 뿐이다 — 조용히 늘어나는 쓰레기를 입구에서 막는다.
    if (canvas->GetScene() != this) return;

    const EntityHandle handle = HandleOf(canvas->m_index);
    if (!handle.IsValid()) return;

    // 키는 **실제 이름**이다. 요청한 이름이 아니라 CreateEntity가 충돌 회피로
    // 확정한 이름을 써야 한다 — 갈라지면 개명된 캔버스를 이름으로 못 찾는다.
    const std::string name = canvas->m_name.ToString();

    CanvasMap[name] = handle;

    // 캐시는 중복을 담지 않는다. 등록이 생명주기 훅(Canvas::OnAddedToScene)으로
    // 옮겨지며 같은 캔버스가 여러 번 들어올 수 있는 경로가 생겼다 — 재부착이
    // 그렇다. 멱등하지 않으면 목록이 조용히 자라고 순회가 같은 캔버스를 두 번 그린다.
    if (std::ranges::find(Canvases, handle) == Canvases.end())
    {
        Canvases.push_back(handle);
    }
}

void Scene::RemoveCanvas(Entity* canvas)
{
    if (!canvas) return;

    if (canvas->GetScene() != this) return;

    const EntityHandle handle = HandleOf(canvas->m_index);
    if (!handle.IsValid()) return;

    if (std::ranges::find(Canvases, handle) == Canvases.end()) return;

    // 핸들이 유효해도 컴포넌트가 있다는 보장은 없다 — 별개 검사다.
    if (auto* canvasCom = canvas->GetComponent<Canvas>())
    {
        for (auto& uiObj : canvasCom->UIObjs)
        {
			if (Entity* uiObjPtr = Resolve(uiObj))
                uiObjPtr->Destroy();
        }
        canvasCom->UIObjs.clear();
    }

    // 지우는 김에 죽은 항목도 함께 걷는다(옛 구현의 expired 청소와 같은 효과).
    std::erase_if(Canvases, [&](const EntityHandle& h)
    {
        return h == handle || !Resolve(h);
    });

    std::erase_if(CanvasMap, [&](const auto& pair)
    {
        return pair.second == handle || !Resolve(pair.second);
    });

    canvas->Destroy();
}

Entity* Scene::FindCanvasName(std::string_view name)
{
    // 옛 구현은 name.data()를 그대로 키로 썼는데, string_view는 널 종단 보장이
    // 없어 부분 뷰가 들어오면 범위 밖을 읽는다. 이 맵은 heterogeneous lookup
    // 설정이 없으므로 한 번 복사해서 찾는다.
    const auto it = CanvasMap.find(std::string(name));
    if (it == CanvasMap.end()) return nullptr;

    return Resolve(it->second);
}


