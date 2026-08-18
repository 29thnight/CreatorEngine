#include "Scene.h"
#include "LifecycleRegistry.h"
#include "VolumeComponent.h"
#include "LifecycleTrace.h"
#include "GameObject.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "LightComponent.h"
#include "MeshRenderer.h"
#include "SpriteRenderer.h"
#include "Terrain.h"
#include "RenderScene.h"
#include "RenderPassData.h"
#include "Camera.h"
#include "Animator.h"
#include "AnimatorSystem.h"
#include "DecalSystem.h"
#include "FoliageSystem.h"
#include "UITickSystem.h"
#include "SoundSystem.h"
#include "CharacterControllerSystem.h"
#include "Skeleton.h"
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
{
    resetObjHandle = SceneManagers->resetSelectedObjectEvent.AddRaw(this, &Scene::ResetSelectedSceneObject);
    m_SceneObjects.reserve(3000);
    m_generations.reserve(3000);
}

Scene::~Scene()
{
    SceneManagers->resetSelectedObjectEvent -= resetObjHandle;
    // 생명주기 델리게이트 15종의 Clear 연쇄가 여기 있었다(PHASE 9-3에서 철거).
    //
    // 종료 행의 자리이기도 했다: Clear가 콜백을 파괴하는데 그 파괴가 같은 델리게이트의
    // Remove를 다시 부르면 재진입 불가 스핀락에서 영원히 돌았다(커밋 c712011f).
    // 델리게이트가 없으니 그 연쇄 자체가 성립하지 않는다.
    //
    // 리스트는 비우기만 하면 된다 — 원소가 raw 포인터라 소멸자 연쇄가 없다.
    m_schedule.Clear();

    m_gameObjectNameSet.clear();
    m_globalDirtySet.clear();
    m_lightComponents.clear();
    m_allMeshRenderers.clear();
    m_staticMeshRenderers.clear();
    m_skinnedMeshRenderers.clear();
    m_lights.clear();
    m_terrainComponents.clear();
    m_foliageComponents.clear();
    m_decalComponents.clear();
    m_spriteRenderers.clear();
    m_SceneObjects.clear();
    m_generations.clear();
    m_freeSlots.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// 슬롯맵 (SceneGraphRedesignPlan 트랙 E1)
// ─────────────────────────────────────────────────────────────────────────────
//
// 예전에는 DestroyGameObjects가 파괴마다 생존자 전원의 인덱스를 재부여했다
// (N-6) — 여기 세 함수가 그것을 대체한다. 생존자의 인덱스는 이제 파괴가
// 일어나도 절대 바뀌지 않는다.
GameObject::Index Scene::AllocateSlot()
{
    if (!m_freeSlots.empty())
    {
        GameObject::Index index = static_cast<GameObject::Index>(m_freeSlots.back());
        m_freeSlots.pop_back();
        return index;
    }

    GameObject::Index index = static_cast<GameObject::Index>(m_SceneObjects.size());
    m_SceneObjects.push_back(nullptr);
    m_generations.push_back(1);
    // 트랜스폼 스토어를 슬롯맵과 평행하게 늘린다(트랙 S, S1) — 프리리스트
    // 재사용 슬롯은 이미 ReleaseSlot이 초기값으로 되돌려 놨으므로 여기 오지 않는다.
    m_transformStore.GrowOne();
    return index;
}

void Scene::ReleaseSlot(GameObject::Index index)
{
    // 루트(0)는 씬 자체가 서 있는 동안 절대 해제하지 않는다.
    if (index == 0) return;
    if (index < 0 || static_cast<size_t>(index) >= m_SceneObjects.size()) return;

    // ── 여기서 ScriptObjectRegistry를 건드리면 안 된다(SceneGraphRedesignPlan
    // 트랙 E4 검토 결과) ──
    //
    // 이 함수는 DestroyGameObjects(진짜 파괴)와 DetachGameObjectHierarchy(DDOL
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
    // (진짜 파괴만 지나가는, 재귀까지 포함하는 유일한 경로 — GameObject.cpp 참고).
    m_SceneObjects[index].reset();

    // 트랜스폼 스토어 슬롯 리셋(트랙 S, S1) — Transform::ResolveStore의 점유자
    // 확인이 이 시점부터 실패하므로(m_SceneObjects[index]가 비었다) 이 리셋을
    // 하지 않아도 낡은 데이터를 읽을 위험은 없지만, 다음 입주자가 재사용
    // 슬롯을 잡았을 때 곧바로 깨끗한 값을 보게 여기서 미리 되돌려 둔다.
    m_transformStore.ResetSlot(static_cast<size_t>(index));

    // 세대 0은 EntityHandle의 "무효"와 겹치므로 건너뛴다.
    ++m_generations[index];
    if (0 == m_generations[index])
    {
        m_generations[index] = 1;
    }
    m_freeSlots.push_back(static_cast<uint32_t>(index));
}

void Scene::UnlinkFromParentChildren(GameObject::Index index)
{
    if (!GameObject::IsValidIndex(index) || static_cast<size_t>(index) >= m_SceneObjects.size())
        return;

    auto& node = m_SceneObjects[index];
    if (!node) return;

    if (GameObject::IsValidIndex(node->m_parentIndex))
    {
        if (auto parent = TryGetGameObject(node->m_parentIndex))
        {
            parent->DetachChildIndex(index);
        }
    }
    // 최상위 오브젝트는 부모 인덱스가 무효인 채로 씬 루트의 children에만 들어
    // 있다(N-13 이전부터의 관례) — 그래서 무조건 한 번 더 시도한다.
    if (!m_SceneObjects.empty() && m_SceneObjects[0])
    {
        m_SceneObjects[0]->DetachChildIndex(index);
    }
}

GameObject* Scene::Resolve(EntityHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    if (handle.index >= m_generations.size()) return nullptr;
    if (m_generations[handle.index] != handle.generation) return nullptr;
    if (handle.index >= m_SceneObjects.size()) return nullptr;

    return m_SceneObjects[handle.index].get();
}

EntityHandle Scene::HandleOf(GameObject::Index index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_generations.size())
        return EntityHandle{};
    if (static_cast<size_t>(index) >= m_SceneObjects.size() || !m_SceneObjects[index])
        return EntityHandle{};

    return EntityHandle{ static_cast<uint32_t>(index), m_generations[index] };
}

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    std::string uniqueName = GenerateUniqueGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);
    sceneObject->m_ownerScene = this;
    sceneObject->Transform_().SetDirty();

    GameObject::Index index = AllocateSlot();
    m_SceneObjects[index] = sceneObject;

    const_cast<GameObject::Index&>(sceneObject->m_index) = index;

    m_SceneObjects[0]->AttachChildIndex(sceneObject->m_index);

    if (!sceneObject->m_tag.ToString().empty())
    {
        TagManagers->AddTagToObject(sceneObject->m_tag.ToString(), sceneObject.get());
    }

    if (!sceneObject->m_layer.ToString().empty())
    {
        TagManagers->AddObjectToLayer(sceneObject->m_layer.ToString(), sceneObject.get());
    }

    return sceneObject;
}

void Scene::AddRootGameObject(std::string_view name)
{
    std::string uniqueName{};

    if (name.empty())
    {
        uniqueName = GenerateUniqueGameObjectName("SampleScene");
    }
    else
    {
        uniqueName = GenerateUniqueGameObjectName(name);
    }

    GameObject::Index index = AllocateSlot();
    auto ptr = std::make_shared<GameObject>(this, uniqueName, GameObjectType::Empty, index, -1);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return;
    }

    m_SceneObjects[index] = ptr;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(std::string_view name, GameObjectType type, GameObject::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }

    if (parentIndex >= m_SceneObjects.size())
    {
        parentIndex = -1;
    }

    std::string uniqueName = GenerateUniqueGameObjectName(name);

    GameObject::Index index = AllocateSlot();

    auto ptr = std::make_shared<GameObject>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return nullptr;
    }
    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    m_SceneObjects[index] = ptr;

    // parentIndex가 무효면(부모를 명시하지 않은 보통의 호출) 씬 루트를 부모로
    // 삼는다 — GetGameObject의 루트 폴백이 암묵적으로 하던 일을 여기서 명시한다.
    // 폴백은 이 의도된 경우와 진짜 오염된 인덱스를 구분하지 못하고 둘 다 조용히
    // root로 흡수했다(N-13) — 여기서는 의도된 경우만 root로 보내고, 나머지는
    // parentObj가 nullptr로 남아 아래 if에서 걸러진다.
    auto parentObj = GetGameObject(parentIndex);
    if (!parentObj)
    {
        parentObj = GetRootObject();
    }
    if (parentObj && parentObj->m_index != index)
    {
        parentObj->AttachChildIndex(index);
    }

    if (!ptr->m_tag.ToString().empty())
    {
        TagManager::GetInstance()->AddTagToObject(ptr->m_tag.ToString(), ptr.get());
    }

    if (!ptr->m_layer.ToString().empty())
    {
        TagManager::GetInstance()->AddObjectToLayer(ptr->m_layer.ToString(), ptr.get());
    }

    return m_SceneObjects[index];
}

std::shared_ptr<GameObject> Scene::LoadGameObject(size_t instanceID, std::string_view name, GameObjectType type, GameObject::Index parentIndex)
{
    if (name.empty())
    {
        return nullptr;
    }

    if (parentIndex >= m_SceneObjects.size())
    {
        parentIndex = GameObject::kSceneRootIndex;
    }

    std::string uniqueName = GenerateUniqueGameObjectName(name);

    GameObject::Index index = AllocateSlot();
    auto ptr = std::make_shared<GameObject>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        ReleaseSlot(index);
        return nullptr;
    }

    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    m_SceneObjects[index] = ptr;

    return m_SceneObjects[index];
}

std::shared_ptr<GameObject> Scene::GetGameObject(GameObject::Index index)
{
    // 예전에는 범위 밖 인덱스를 조용히 루트(m_SceneObjects[0])로 흘려보냈다
    // (N-13) — 계층 오염이 몇 달을 숨어 있던 원인이다. 무효한 요청은 이제
    // TryGetGameObject와 같은 의미로 nullptr을 돌려준다.
    if (index >= 0 && static_cast<size_t>(index) < m_SceneObjects.size())
    {
        return m_SceneObjects[index];
    }

    return nullptr;
}

std::shared_ptr<GameObject> Scene::TryGetGameObject(GameObject::Index index)
{
    if (index == GameObject::INVALID_INDEX || index < 0)
    {
        return nullptr;
    }
    if (static_cast<size_t>(index) < m_SceneObjects.size())
    {
        return m_SceneObjects[index];
    }
    return nullptr;
}

void Scene::DetachGameObjectHierarchy(GameObject* root)
{
    if (!root) return;
    Scene* origin = root->GetScene();
    if (origin != this) return;
    // 씬 루트(0)는 이 경로로 오면 안 된다 — 아래에서 슬롯 해제 단일점을 타므로,
    // 다른 파괴 경로와 마찬가지로 여기서도 방어적으로 막는다.
    if (0 == root->m_index) return;

    // breadth-first (인덱스 재배열 없이 안전하게 순회)
    std::vector<GameObject::Index> queue;
    queue.push_back(root->m_index);

    // 루트부터 부모/씬 루트 children 에서 분리
    UnlinkFromParentChildren(root->m_index);

    for (size_t qi = 0; qi < queue.size(); ++qi)
    {
        auto idx = queue[qi];
        auto node = TryGetGameObject(idx);
        if (!node) continue;

        // 자식 enqueue: 유효 인덱스만 복사
        std::ranges::copy_if(
            node->m_childrenIndices,
            std::back_inserter(queue),
            [](GameObject::Index i) { return GameObject::IsValidIndex(i); }
        );

        // 태그/레이어에서 분리 (원 씬 검색에서 빠지도록)
        if (!node->m_tag.ToString().empty())
        {
            TagManager::GetInstance()->RemoveTagFromObject(node->m_tag.ToString(), node.get());
        }
        if (!node->m_layer.ToString().empty())
        {
            TagManager::GetInstance()->RemoveObjectFromLayer(node->m_layer.ToString(), node.get());
        }

        // 부모 링크 절단 (월드 유지)
        node->SetParentIndex(GameObject::INVALID_INDEX);

        // 씬 이탈 통지(트랙 L1) — 신설 축이라 대응하는 옛 훅이 없다. 레거시
        // 컴포넌트는 기본 구현(빈 함수)만 타므로 92 사건 기준선은 그대로다.
        // 대칭짝은 AttachExistingGameObject의 OnAddedToScene.
        for (auto& component : node->m_components)
        {
            if (component) component->OnRemovingFromScene();
        }
        node->m_scenePhase = ScenePhase::Attached;

        // 슬롯 해제 단일점(트랙 E1) — tombstone+세대 증가+free 리스트 등록을
        // DestroyGameObjects와 공유한다. 재부착은 AttachExistingGameObject가
        // 같은 free 리스트를 재사용해 채운다.
        ReleaseSlot(idx);
    }
}

// === C안 구현: 이름 충돌 방지 ===
std::string Scene::MakeUniqueName(std::string_view base)
{
    std::string name(base);
    if (name.empty()) name = "GameObject";
    if (!GetGameObject(name)) return name;
    int n = 1;
    std::string trial;
    do {
        trial = name + " (" + std::to_string(n++) + ")";
    } while (GetGameObject(trial));
    return trial;
}

// === C안 구현: 단일 객체 부착 ===
GameObject::Index Scene::AttachExistingGameObject(std::shared_ptr<GameObject> go, GameObject::Index parentIndex)
{
    if (!go) return GameObject::INVALID_INDEX;

    // 이 씬 기준 유니크 네임 보장
    if (auto existed = GetGameObject(go->GetHashedName().ToString()); existed)
        go->SetName(MakeUniqueName(go->GetHashedName().ToString()));

    // 이 씬에 소속
    go->m_ownerScene = this;

    // 새 인덱스 할당 — free 리스트가 있으면 재사용한다(트랙 E1).
    GameObject::Index newIndex = AllocateSlot();
    go->m_index = newIndex;
    m_SceneObjects[newIndex] = go;

    // Tag/Layer 재등록
    if (!go->m_tag.ToString().empty())
        TagManager::GetInstance()->AddTagToObject(go->m_tag.ToString(), go.get());
    if (!go->m_layer.ToString().empty())
        TagManager::GetInstance()->AddObjectToLayer(go->m_layer.ToString(), go.get());

    // Transform 부모 세팅 (루트 규약: INVALID_INDEX == 루트)
    if (GameObject::IsValidIndex(parentIndex))
    {
        go->SetParentIndex(parentIndex);
        if (auto parent = TryGetGameObject(parentIndex))
        {
            parent->AttachChildIndex(newIndex);
        }
    }
    else
    {
        go->SetParentIndex(GameObject::INVALID_INDEX);
        // 씬 루트 children 연결
        if (!m_SceneObjects.empty() && m_SceneObjects[0])
        {
            m_SceneObjects[0]->AttachChildIndex(newIndex);
        }
    }

    // 씬 편입 통지(트랙 L1) — DDOL 재부착은 이미 Awake가 끝난 컴포넌트가
    // 대부분이라 pendingAwake 큐(이미 지난 정거장)를 다시 타지 않는다. 대칭짝은
    // DetachGameObjectHierarchy의 OnRemovingFromScene. 신설 축이라 대응하는 옛
    // 훅이 없고, 레거시 컴포넌트는 기본 구현(빈 함수)만 타므로 92 사건 기준선은
    // 그대로다.
    go->m_scenePhase = ScenePhase::InScene;
    for (auto& component : go->m_components)
    {
        if (component) component->OnAddedToScene();
    }

    // 필요 시 컴포넌트 쪽 씬/이벤트 갱신은 호출측(매니저)에서 일괄 처리
    return newIndex;
}

// === C안 구현: 서브트리 부착 ===
std::unordered_map<GameObject::Index, GameObject::Index>
Scene::AttachExistingGameObjectHierarchy(const std::vector<std::shared_ptr<GameObject>>& roots)
{
    std::unordered_map<GameObject::Index, GameObject::Index> remap;
    if (roots.empty()) return remap;

    // BFS로 루트별 서브트리 전개 (부모 → 자식 순서 보장)
    std::vector<std::shared_ptr<GameObject>> ordered;
    ordered.reserve(roots.size() * 4);
    std::queue<std::shared_ptr<GameObject>> q;
    for (auto& r : roots) if (r) q.push(r);
    while (!q.empty())
    {
        auto cur = q.front(); q.pop();
        ordered.push_back(cur);
        for (auto childIdx : cur->m_childrenIndices)
        {
            if (auto ch = TryGetGameObject(childIdx))
                q.push(ch);
        }
    }

    // oldParent → newParent 를 remap 하면서 부착
    for (auto& node : ordered)
    {
        auto oldIdx = node->m_index;
        auto oldParent = node->m_parentIndex;
        GameObject::Index newParent =
            remap.count(oldParent) ? remap[oldParent] :
            GameObject::INVALID_INDEX;

        auto newIdx = AttachExistingGameObject(node, newParent);
        remap[oldIdx] = newIdx;
    }
    return remap;
}

std::shared_ptr<GameObject> Scene::GetGameObject(std::string_view name)
{
    HashingString hashedName(name.data());
    for (auto& obj : m_SceneObjects)
    {
        if (obj && obj->GetHashedName() == hashedName)
        {
            return obj;
        }
    }
    return nullptr;
}

void Scene::DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    if (nullptr == sceneObject)
    {
        return;
    }

    RemoveGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->Destroy();
}

void Scene::DestroyGameObject(GameObject::Index index)
{
    if (index < m_SceneObjects.size())
    {
        auto obj = m_SceneObjects[index];
        if (nullptr != obj)
        {
            RemoveGameObjectName(obj->GetHashedName().ToString());
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

    auto renderScene = SceneManagers->GetRenderScene();

    // 2단계(카메라별 UI)가 쓰는 목록만 여기 남긴다 — 프록시 커밋이 쓰던 나머지
    // 다섯은 CommitRenderProxies 안으로 옮겨 갔다. 값 복사인 이유는 기존 규약
    // 그대로다(순회 중 원본이 바뀔 수 있다).
    std::vector<ImageComponent*> imageComponents = UIManagers->Images;
    std::vector<TextComponent*> textComponents = UIManagers->Texts;
    std::vector<SpriteSheetComponent*> spriteSheetComponents = UIManagers->SpriteSheets;

    // ── 1단계: 렌더 프록시 갱신 (카메라와 무관) ──
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

    // ── 2단계: 카메라별 UI 렌더 데이터 ──
    //
    // ★ 여기 있던 컬링을 통째로 걷었다 (RenderSceneViewPlan ③).
    //
    //   카메라마다 옥트리 질의(FrustumCullFrontToBack)를 한 뒤 그 결과
    //   visibleMeshes를 아무도 읽지 않고 버렸고, 이어서 스레드풀 태스크
    //   일곱이 컴포넌트 리스트를 다시 전수 순회하며 AABB 교차를 재서
    //   PushCullData/PushShadowRenderData에 쌓았다 — 그 버퍼를 읽는 코드도
    //   저장소에 없었다. 카메라가 늘수록 O(카메라 x 전체 메시)로 커지는
    //   계산을 매 프레임 내고 드로우는 하나도 줄이지 못했다.
    //
    //   실제 컬링은 이제 렌더 쪽에서 뷰가 한다 — 프록시의 월드 AABB와
    //   뷰 절두체로 EnhancedSceneRendererLive가 자른다. 게임 스레드가
    //   카메라를 알 이유가 사라졌다.
    //
    // 아래 UI 밀어넣기는 남는다. GetUIRenderDataBuffer에 실소비자가 있다.
    for (auto camera : CameraManagement->GetCameras())
    {
        if (!camera) continue;
        if (!RenderPassData::VaildCheck(camera.get())) continue;
        auto data = RenderPassData::GetData(camera.get());

		//여기 부터는 UI -> 컬링하는 부분이 아님 분리 필요 -> UI 렌더링 데이터 푸시
        WorkerPools->Enqueue([=]
        {
            for (auto& image : imageComponents)
            {
                if (false == image->IsEnabled() || false == image->GetOwner()->IsEnabled()) continue;

                auto owner = image->GetOwner();
                if (nullptr == owner) continue;

                auto scene = owner->GetScene();

                // (G) UI 중복 렌더 가드:
                //  - 같은 씬이면 렌더
                //  - DDOL이면 "활성 씬 소속"인 경우에만 렌더
                if (scene && (scene == this ||
                    (owner->IsDontDestroyOnLoad() && scene == SceneManagers->GetActiveScene())))
                {
                    data->PushUIRenderData(image->GetInstanceID());
                }
            }
        });

        WorkerPools->Enqueue([=]
        {
            for (auto& text : textComponents)
            {
                if (false == text->IsEnabled() || false == text->GetOwner()->IsEnabled()) continue;

                auto owner = text->GetOwner();
                if (nullptr == owner) continue;

                auto scene = owner->GetScene();

                if (scene && (scene == this ||
                    (owner->IsDontDestroyOnLoad() && scene == SceneManagers->GetActiveScene())))
                {
                    data->PushUIRenderData(text->GetInstanceID());
                }
            }
        });

        WorkerPools->Enqueue([=]
        {
            for (auto& spriteSheet : spriteSheetComponents)
            {
                if (spriteSheet->IsDestroyMark() || false == spriteSheet->IsEnabled() || false == spriteSheet->GetOwner()->IsEnabled()) continue;
                auto owner = spriteSheet->GetOwner();
                if (nullptr == owner) continue;
                auto scene = owner->GetScene();
                if (scene && (scene == this ||
                    (owner->IsDontDestroyOnLoad() && scene == SceneManagers->GetActiveScene())))
                {
                    data->PushUIRenderData(spriteSheet->GetInstanceID());
                }
            }
        });

        WorkerPools->NotifyAllAndWait();
    }
}

void Scene::InternalPauseUpdateForUI()
{
    if (SceneManagers->IsGamePaused())
    {
        float deltaTime = Time->GetElapsedSeconds();
        auto canvasObj = UIManagers->CurCanvas.lock();
        if (!canvasObj) return;

        AllUIUpdateWorldMatrix();

        auto canvas = canvasObj->GetComponent<Canvas>();
        for (const auto& weak : canvas->UIObjs)
        {
            auto obj = weak.lock();
            if (obj)
            {
                auto imageComponents = obj->GetComponents<ImageComponent>();
                for (const auto& imageComponent : imageComponents)
                {
                    imageComponent->Update(deltaTime);
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
                    inputComponent->Update(deltaTime);
                }

            }
        }
    }
}

std::vector<std::shared_ptr<GameObject>> Scene::CreateGameObjects(size_t createSize, GameObject::Index parentIndex)
{
    std::vector<std::shared_ptr<GameObject>> created;
    created.reserve(createSize);

    // generate_n(back_inserter(...)) 로 정확히 createSize개 생성
    std::generate_n(
        std::back_inserter(created),
        createSize,
        [&] { return CreateGameObject("default", GameObjectType::Empty, parentIndex); }
    );

    return created;
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

    // 기록용 타입 이름.
    //
    // 델리게이트 경로는 T를 알아 컴파일 타임 이름을 썼지만, 여기서는 Component*뿐이다.
    // 리플렉션 레지스트리가 typeID로 이름을 들고 있으므로 그것을 쓴다 —
    // 두 경로가 **같은 문자열**을 남겨야 9-0 기준선과 대조가 성립한다.
    // Meta::Type은 T::Reflect()가 돌려주는 정적 객체라 c_str()의 수명이 안전하다.
    const char* TraceTypeName(Component* component)
    {
        const Meta::Type* type = Meta::Find(component->GetTypeID().m_ID_Data);
        return (nullptr != type) ? type->name.c_str() : "?";
    }
}

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

    if (mask & Lifecycle::Bit_Update)      m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::Update);
    if (mask & Lifecycle::Bit_LateUpdate)  m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::LateUpdate);
    if (mask & Lifecycle::Bit_FixedUpdate) m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::FixedUpdate);

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
    return RegistryCounts{
        m_schedule.PendingAwakeList().size(), m_schedule.PendingStartList().size(),
        m_schedule.UpdateList().size(), m_schedule.LateUpdateList().size(), m_schedule.FixedUpdateList().size(),
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
        GameObject* owner = component->GetOwner();
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
        LIFECYCLE_TRACE(Lifecycle::Phase::Awake, TraceTypeName(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->OnInitialized();

        // Scene 진입 통지(트랙 L1) — phase catch-up. 지금 엔티티는 생성과 동시에
        // 씬에 들어가므로 이 조건은 사실상 항상 참이다. 대응하는 옛 훅이 없어
        // 관측(LIFECYCLE_TRACE)에는 남기지 않는다 — DDOL 재부착의 OnAddedToScene은
        // 이미 Awake가 끝난 컴포넌트가 대부분이라 이 큐를 다시 타지 않고
        // Scene::AttachExistingGameObject가 직접 부른다(대칭짝은
        // DetachGameObjectHierarchy의 OnRemovingFromScene).
        if (owner->m_scenePhase >= ScenePhase::InScene)
        {
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
        GameObject* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled())
        {
            m_schedule.SubscribeImplicit(component, SystemSchedule::Phase::PendingStart);
            continue;
        }

        component->MarkLifecycleState(Component::State_StartCalled);
        LIFECYCLE_TRACE(Lifecycle::Phase::Start, TraceTypeName(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->OnBeginSimulation();
    }
}

void Scene::FireReentrancyStress(bool midTraversal)
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
        for (size_t i = 1; i < m_SceneObjects.size() && destroyed < count; ++i)
        {
            const auto& owned = m_SceneObjects[i];
            if (!owned || owned->IsDestroyMark()) continue;
            DestroyGameObject(owned);
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
        // 덤으로 '순회 중 GameObject 생성'까지 함께 시험한다.
        //
        // 확인할 불변식: 새 컴포넌트는 pendingAwake로 가야 하고 지금 도는 update
        // 리스트에 끼어들면 안 된다. 끼어들면 이번 바퀴의 순회가 무효화된다.
        for (int n = 0; n < count; ++n)
        {
            auto created = CreateGameObject("StressReentrant_" + std::to_string(n));
            if (!created) continue;
            created->AddComponent<VolumeComponent>();
            ++added;
        }
    }

    Debug->LogWarning("[Lifecycle] 재진입 시험 발화(" + std::string(midTraversal ? "순회 중" : "리스트 비어 순회 밖")
        + ") — 파괴 " + std::to_string(destroyed)
        + " · 생성+컴포넌트 " + std::to_string(added)
        + " (update 리스트 " + std::to_string(m_schedule.UpdateList().size())
        + " · pendingAwake " + std::to_string(m_schedule.PendingAwakeList().size()) + ")");
}

void Scene::ArmReentrancyStress(StressKind kind, int count)
{
    g_stressKind = static_cast<int>(kind);
    g_stressCount = (count > 0) ? count : 1;
    g_stressArmed = true;
}

void Scene::RegistryTick(std::vector<Component*>& list, Lifecycle::PhaseBits phase, float delta)
{
    // 인덱스로 돈다. 순회 중 리스트가 커질 수 있기 때문이다(AddComponent는 pending
    // 큐로 가므로 이 리스트는 안 커지지만, 규약을 코드로 못 박아 두는 편이 낫다).
    // 리스트가 줄어드는 일은 없다 — 제거는 프레임 끝 한 지점에서만 일어난다.
    //
    // 재진입 시험(9-9)은 이 루프의 한가운데에서 한 번만 터진다. 무장 여부는 루프
    // 진입 전에 한 번 읽고, 평상시에는 아래 비교 하나가 비용의 전부다.
    const bool stressThisPass = g_stressArmed && (Lifecycle::Bit_Update == phase);
    const size_t stressAt = stressThisPass ? (list.size() / 2) : SIZE_MAX;

    for (size_t i = 0; i < list.size(); ++i)
    {
        if (i == stressAt) FireReentrancyStress(true);

        Component* component = list[i];
        if (nullptr == component) continue;

        GameObject* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled()) continue;

        switch (phase)
        {
        case Lifecycle::Bit_Update:
            LIFECYCLE_TRACE(Lifecycle::Phase::Update, TraceTypeName(component),
                owner->m_name.ToString().c_str(), component->GetInstanceID());
            component->Update(delta);
            break;
        case Lifecycle::Bit_LateUpdate:
            LIFECYCLE_TRACE(Lifecycle::Phase::LateUpdate, TraceTypeName(component),
                owner->m_name.ToString().c_str(), component->GetInstanceID());
            component->LateUpdate(delta);
            break;
        case Lifecycle::Bit_FixedUpdate:
            LIFECYCLE_TRACE(Lifecycle::Phase::FixedUpdate, TraceTypeName(component),
                owner->m_name.ToString().c_str(), component->GetInstanceID());
            component->FixedUpdate(delta);
            break;
        default:
            break;
        }
    }

    // 무장이 조용히 증발하지 않게 한다.
    //
    // 처음에는 순회 한복판에서만 터뜨렸는데, 앞선 시험이 카메라·라이트를 파괴해
    // update 리스트가 비면 루프 자체가 돌지 않아 이후 무장이 아무 일도 없이 사라졌다.
    // 로그에는 '무장했다'만 남고 '터졌다'가 없어, 시험이 도는지 아닌지 구분되지 않았다 —
    // 확인하지 못한 것과 확인했고 문제없는 것은 다르다.
    //
    // 리스트가 비면 '순회 중'이라는 상황 자체가 없으므로 순회 밖에서 터뜨리되,
    // 그 사실을 로그에 명시한다.
    if (stressThisPass && g_stressArmed) FireReentrancyStress(false);
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
        GameObject* owner = component->GetOwner();
        const bool dying = (nullptr == owner) || owner->IsDestroyMark() || component->IsDestroyMark();
        if (dying) doomed.push_back(component);
    }

    for (Component* component : doomed)
    {
        GameObject* owner = component->GetOwner();

        // 이름을 값으로 붙든다.
        //
        // ToString()은 값을 돌려주므로 그 c_str()을 const char*에 담으면 문장이
        // 끝나는 순간 뜬다. 실제로 기록에 빈 이름이 찍혀 드러났다 — 크래시가 아니라
        // '이름만 비는' 모습이라 눈으로는 알아채기 어려운 종류다.
        const std::string ownerName = (nullptr != owner) ? owner->m_name.ToString() : std::string("?");

        // 파괴 직전 축소(트랙 L1) — 실제 파괴(OnUninitializing, 옛 OnDestroy 브리지)
        // 앞에 Simulation 종료·씬 이탈을 먼저 통지한다. 대응하는 옛 훅이 없어
        // 레거시 컴포넌트는 기본 구현(빈 함수)만 타므로 LIFECYCLE_TRACE 기록에는
        // 흔적을 남기지 않는다 — 그래서 트레이스 호출은 OnUninitializing 자리
        // 그대로 둔다.
        component->OnEndSimulation();
        component->OnRemovingFromScene();

        LIFECYCLE_TRACE(Lifecycle::Phase::OnDestroy, TraceTypeName(component), ownerName.c_str(), component->GetInstanceID());
        component->OnUninitializing();

        UnregisterComponent(component);
    }
}

void Scene::Awake()
{
    // Awake 단계가 pendingStart까지 소진한다 — Awake 직후에 Start를 부르는 것이 규약이다.
    RegistryDrainAwakeAndStart();
}

void Scene::OnEnable()
{
    // 비어 있다. 활성 전이는 Component::SetEnabled가 그 자리에서 처리한다(9-2).
    // 매 프레임 전체를 훑으며 '바뀐 게 있나' 묻던 스캔이 사라진 자리다.
}

void Scene::Start()
{
    // 비어 있다. Awake 단계가 pendingStart까지 소진하므로(위 Scene::Awake),
    // 여기서 또 부르면 같은 프레임에 Start가 두 번 돈다.
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
    PROFILE_CPU_BEGIN("fixedBroadcast");
    RegistryTick(m_schedule.FixedUpdateList(), Lifecycle::Bit_FixedUpdate, deltaSecond);
    PROFILE_CPU_END();
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

    PROFILE_CPU_BEGIN("UpdateEvent");
    RegistryTick(m_schedule.UpdateList(), Lifecycle::Bit_Update, deltaSecond);
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
    RegistryTick(m_schedule.LateUpdateList(), Lifecycle::Bit_LateUpdate, deltaSecond);

    // 트랙 C3 잔여 — LateUpdate를 오버라이드하던 둘. 옛 위치(LateUpdateList 안)와
    // 같은 창(RegistryTick 이후 · UpdateRenderData 이전)을 지킨다.
    SoundSystems->LateUpdate(deltaSecond);
    CharacterControllerSystems->LateUpdate(deltaSecond);

    UpdateRenderData();
}

void Scene::OnDisable()
{
    PROFILE_CPU_BEGIN("OnDisable");
    // OnEnable과 같은 이유로 비어 있다 — SetEnabled가 처리한다.
    PROFILE_CPU_END();
}

void Scene::OnDestroy()
{
    PROFILE_CPU_BEGIN("OnDestroyBroadcast");
    // 이 자리가 프레임 끝의 파괴 지점이다 — 바로 아래에서 DestroyComponents와
    // DestroyGameObjects가 실제 해제를 하므로, 그 직전이 OnDestroy를 부를 마지막 기회다.
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
    PROFILE_CPU_BEGIN("DestroyGameObjects");
    DestroyGameObjects();
    PROFILE_CPU_END();
    //여기서 병렬처리
    if(!m_AIFuture.valid())
    {
        float deltaSecond = Time->GetElapsedSeconds();
        m_AIFuture = std::async(std::launch::async, [deltaSecond]
        {
            AIManagers->InternalAIUpdate(deltaSecond);
        });
    }
}

void Scene::AllDestroyMark()
{
    for (const auto& obj : m_SceneObjects)
    {
        if (obj && !obj->IsDestroyMark() && !obj->IsDontDestroyOnLoad())
            obj->Destroy();
    }
}

void Scene::ResetSelectedSceneObject()
{
    m_selectedSceneObject = nullptr;
    m_selectedSceneObjects.clear();
}

void Scene::AddSelectedSceneObject(GameObject* sceneObject)
{
    if (!sceneObject) return;

    if (std::ranges::find(m_selectedSceneObjects, sceneObject) == m_selectedSceneObjects.end())
    {
        m_selectedSceneObjects.push_back(sceneObject);
        m_selectedSceneObject = sceneObject;
    }
}

void Scene::RemoveSelectedSceneObject(GameObject* sceneObject)
{
    if (!sceneObject) return;
    auto it = std::ranges::find(m_selectedSceneObjects, sceneObject);
    if (it != m_selectedSceneObjects.end())
    {
        m_selectedSceneObjects.erase(it);
        if (m_selectedSceneObject == sceneObject)
        {
            m_selectedSceneObject = m_selectedSceneObjects.empty() ? nullptr : m_selectedSceneObjects.back();
        }
    }
}

void Scene::ClearSelectedSceneObjects()
{
    m_selectedSceneObjects.clear();
    m_selectedSceneObject = nullptr;
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

// UpdateLight가 여기 있었다 — m_lights 전체를 매 프레임 훑어 LightProperties
// 배열로 복사했고, 그것이 렌더러가 광원을 보는 유일한 통로였다. 광원이
// 등록/해제 기반 프록시(LightRenderProxy)로 옮겨 가면서 소비자가 사라졌다.
//
// 아래 남은 것은 편집기 부기다: m_lightIndex(기즈모가 "메인 라이트"를 가리는
// 데 쓴다)를 발급하고, DestroyLight가 유효 슬롯을 압축하며 그 인덱스를
// 다시 맞춘다. 그리는 값은 더 이상 여기를 지나지 않는다.

std::pair<size_t, Light&> Scene::AddLight()
{
    Light& light = m_lights.emplace_back();
    light.m_lightStatus = LightStatus::Enabled;
    size_t index = m_lights.size() - 1;

    return std::pair<size_t, Light&>(index, light);
}

Light& Scene::GetLight(size_t index)
{
    if (index > m_lights.size() || 0 == m_lights.size())
    {
        m_lights.resize(index + 1);
    }

    return m_lights[index];
}

void Scene::RemoveLight(size_t index)
{
    if (index < m_lights.size())
    {
        m_lights[index].m_lightType = LightType_InVaild;
        m_lights[index].m_lightStatus = LightStatus::Disabled;
        m_lights[index].m_intencity = 0.f;
        m_lights[index].m_color = { 0,0,0,0 };
    }
}

void Scene::DestroyLight()
{
    std::unordered_map<size_t, size_t> indexRemap;
    std::vector<Light> newLights;
    bool isFirstDirectional = false;

    newLights.reserve(m_lights.size());

    for (size_t i = 0; i < m_lights.size(); ++i)
    {
        if (m_lights[i].m_lightType != LightType_InVaild)
        {
            indexRemap[i] = newLights.size();
            newLights.push_back(m_lights[i]);
        }
    }

    m_lights = std::move(newLights);

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
        push_unique(m_terrainColliderComponents, ptr);

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
        std::erase_if(m_terrainColliderComponents, [ptr](const auto& terrain) { return terrain == ptr; });

        PhysicsManagers->RemoveCollider(ptr);
    }
}

void Scene::DestroyGameObjects()
{
    std::unordered_set<uint32_t> deletedIndices;
    for (const auto& obj : m_SceneObjects)
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
        if (index >= m_SceneObjects.size()) continue;

        auto& obj = m_SceneObjects[index];
        if (!obj) continue;

        // 자식들의 부모 링크를 끊는다. 자식도 함께 파괴 대상이면 곧 자신의
        // 차례에 tombstone되므로, 여기서는 생존 자식에게만 실질적인 효과가 있다.
        for (auto childIdx : obj->m_childrenIndices)
        {
            if (GameObject::IsValidIndex(childIdx) &&
                static_cast<size_t>(childIdx) < m_SceneObjects.size() &&
                m_SceneObjects[childIdx])
            {
                m_SceneObjects[childIdx]->SetParentIndex(GameObject::INVALID_INDEX);
            }
        }
        obj->ClearChildren();

        // 부모(또는 씬 루트)의 children 목록에서 자신을 뗀다 — 안 하면 죽은
        // 인덱스가 남아, 슬롯이 재사용됐을 때 엉뚱한 객체를 가리킨다.
        UnlinkFromParentChildren(static_cast<GameObject::Index>(index));

        ReleaseSlot(static_cast<GameObject::Index>(index));
    }
}

void Scene::DestroyComponents()
{
    for (auto& obj : m_SceneObjects)
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

std::string Scene::GenerateUniqueGameObjectName(const std::string_view& name)
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
    while (m_gameObjectNameSet.contains(uniqueName))
    {
        uniqueName = baseName + " (" + std::to_string(count++) + ")";
    }
    m_gameObjectNameSet.insert(uniqueName);
    return uniqueName;
}

void Scene::RemoveGameObjectName(const std::string_view& name)
{
    m_gameObjectNameSet.erase(name.data());
}

// 순회 진입 가드 단일화 구현(선언은 Scene.h — 이유·수렴 안 시킨 두 곳의 근거도
// 거기 있다). Debug 전역이 필요해 Scene.h가 아니라 여기서 정의하고, 실제로
// 쓰이는 두 키 타입(GameObject::Index·GameObject*)만 명시 인스턴스화한다 —
// 이 TU 밖에서는 못 쓴다는 뜻이고, Scene.h에 인라인으로 두면 Debug 전역을
// include하지 않은 다른 TU에서 컴파일이 깨질 위험이 있다(유니티 빌드).
template<typename Key>
bool Scene::TryEnterTraversal(std::unordered_set<Key>& visited, const Key& key,
    int depth, const char* traversalLabel, std::string_view nodeName)
{
    if (!visited.insert(key).second) return false;

    if (depth > kTraversalMaxDepth)
    {
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            Debug->LogError(std::string(traversalLabel)
                + "가 최대 깊이를 넘었다 — 계층이 지나치게 깊거나 순환한다: "
                + std::string(nodeName));
        }
        return false;
    }
    return true;
}
template bool Scene::TryEnterTraversal<GameObject::Index>(
    std::unordered_set<GameObject::Index>&, const GameObject::Index&, int, const char*, std::string_view);
template bool Scene::TryEnterTraversal<GameObject*>(
    std::unordered_set<GameObject*>&, GameObject* const&, int, const char*, std::string_view);

void Scene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model, bool parentChanged,
    std::unordered_set<GameObject::Index>* visited, int depth)
{
    if (objIndex == GameObject::INVALID_INDEX || objIndex < 0 ||
        static_cast<size_t>(objIndex) >= m_SceneObjects.size())
    {
        return;
    }

    const auto& obj = m_SceneObjects[objIndex];

    if (!obj || obj->IsDestroyMark())
    {
        return;
    }

    // AllUpdateWorldMatrix가 루트 자식 단위로 std::execution::par 병렬 실행하므로
    // 방문집합을 공유하면 레이스가 난다 — 최초 호출(visited==nullptr)에서만 이
    // 스택 프레임에 만들어 재귀 내내 포인터로 물려준다. optional인 이유: MSVC의
    // unordered_set은 기본 생성자가 버킷을 즉시 할당해, 재귀 호출마다 만들면
    // 핫패스에 노드당 할당이 얹힌다. 계층에 순환이 있어도 여기서 멈춘다.
    std::optional<std::unordered_set<GameObject::Index>> localVisited;
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

    switch (obj->GetType())
    {
    case GameObjectType::UI:
    {
        // UI는 트랜스폼 행렬 대신 rect로 배치된다. 레이아웃은 UpdateUILayout이
        // 전담하므로 여기서는 아무것도 하지 않고 자식 순회만 이어 간다(PHASE 7-5).
        break;
    }
    case GameObjectType::Bone:
    {
        const auto& rootObj = TryGetGameObject(obj->m_rootIndex);
        if (!rootObj)
        {
            return;
        }
        const auto& animator = rootObj->GetComponent<Animator>();
        if (!animator || !animator->m_Skeleton || !animator->IsEnabled())
        {
            return;
        }
        const auto bone = animator->m_Skeleton->FindBone(obj->RemoveSuffixNumberTag());
        obj->Transform_().SetAndDecomposeMatrix(XMMatrixMultiply(bone ?
            animator->m_localTransforms[bone->m_index] : obj->Transform_().GetLocalMatrix(), model));
        // 애니메이션이 매 프레임 로컬 행렬을 갈아치우므로 dirty 플래그에 기대지
        // 않고 항상 재계산·전파한다(S2 범위 밖 — C3가 애니메이션 자체는 손댄다).
        childParentChanged = true;
        break;
    }
    default:
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
        // GetGameObjectRaw로 "이 슬롯의 진짜 점유자가 나인가" 확인 →
        // GetTransformStore). 게이트가 그걸 노드마다 두 번(dirty·worldChanged)
        // 물면, 아껴 낸 decompose보다 재해석이 더 비싸진다 — Release 실측에서
        // 10,000개·10% 이동 시나리오가 옛 경로보다 약 4% **느렸다**. 이 순회는
        // 바로 위에서 m_SceneObjects[objIndex]로 obj를 꺼냈으므로 점유자 확인이
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
            model = hasStoreSlot ? Mathf::xMatrix(m_transformStore.worldMatrix[storeSlot])
                                 : obj->Transform_().GetWorldMatrix();
            break;
        }

        if (localDirty)
        {
            auto renderer = obj->GetComponent<MeshRenderer>();
            if (renderer)
            {
                renderer->SetNeedUpdateCulling(true);
            }
        }
        model = XMMatrixMultiply(obj->Transform_().GetLocalMatrix(), model);
        obj->Transform_().SetAndDecomposeMatrix(model);
        childParentChanged = true;
        break;
    }
    }

    for (auto& childIndex : obj->m_childrenIndices)
    {
        if (childIndex == obj->m_index) continue;
        UpdateModelRecursive(childIndex, model, childParentChanged, visited, depth + 1);
    }
}

void Scene::LayoutUINode(GameObject* obj, const Mathf::Rect& parentRect,
    float parentScale, bool parentChanged, bool isTopLevel, int depth,
    std::unordered_set<GameObject*>& visited)
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

    Mathf::Rect childRect = parentRect;
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
        const Mathf::Rect screenRect = RectTransformComponent::GetScreenRootRect();
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

    for (auto childIndex : obj->m_childrenIndices)
    {
        if (childIndex == obj->m_index) continue;
        LayoutUINode(GameObject::FindIndex(childIndex), childRect, childScale,
            childChanged, childIsTopLevel, depth + 1, visited);
    }
}

void Scene::UpdateUILayout()
{
    if (m_SceneObjects.empty()) return;

    const Mathf::Rect screenRect = RectTransformComponent::GetScreenRootRect();
    std::unordered_set<GameObject*> visited;

    // 씬 루트의 자식부터 한 번만 훑는다. 캔버스 구동도, 캔버스 밑에 없는 UI도
    // 전부 이 한 순회 안에서 처리된다.
    for (auto& rootIndex : m_SceneObjects[0]->m_childrenIndices)
    {
        LayoutUINode(GameObject::FindIndex(rootIndex), screenRect, 1.f,
            /*parentChanged=*/false, /*isTopLevel=*/true, 0, visited);
    }

    // 루트에서 닿지 않는 캔버스가 있으면 여기서 줍는다. 예전 구현이 Canvases 목록을
    // 따로 돌았으므로, 그런 캔버스가 있어도 동작이 달라지지 않게 남겨 둔다.
    for (auto& weakCanvas : Canvases)
    {
        auto canvasObj = weakCanvas.lock();
        if (!canvasObj || canvasObj->IsDestroyMark()) continue;
        if (visited.count(canvasObj.get())) continue;

        LayoutUINode(canvasObj.get(), screenRect, 1.f, false, true, 0, visited);
    }
}

void Scene::LayoutUISubtree(GameObject* root)
{
    if (nullptr == root) return;

    // 부모 기준은 컴포넌트와 같은 규칙으로 찾는다 — 여기에 (0,0,W,H)를 따로 적어
    // 두었던 곳들이 캔버스 규약과 어긋나 있었다(PHASE 7-2에서 두 곳, 7-5에서 세 곳).
    Mathf::Rect parentRect = RectTransformComponent::GetScreenRootRect();
    float parentScale = 1.f;
    bool isTopLevel = true;

    if (GameObject::IsValidIndex(root->m_parentIndex))
    {
        if (GameObject* parent = GameObject::FindIndex(root->m_parentIndex))
        {
            if (auto* parentRT = parent->GetComponent<RectTransformComponent>())
            {
                parentRect = parentRT->GetWorldRect();
                parentScale = parentRT->GetLayoutScale();
                isTopLevel = false;
            }
        }
    }

    std::unordered_set<GameObject*> visited;
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

    std::unordered_map<GameObject*, EBodyType> m_bodyType;

    for (auto& rigid : m_rigidBodyComponents)
    {
        auto gameObject = rigid->GetOwner();
        m_bodyType[gameObject] = rigid->GetBodyType();
    }

    std::unordered_set<GameObject*> linkCompleteSet;
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

    if (m_SceneObjects.empty()) return;

    auto& rootObjects = m_SceneObjects[0]->m_childrenIndices;

    auto updateFunc = [this](GameObject::Index index)
    {
        UpdateModelRecursive(index, XMMatrixIdentity());
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

void Scene::AddCanvas(const std::shared_ptr<GameObject>& canvas)
{
    if (!canvas) return;

    const std::string name = canvas->m_name.ToString();

    // Map 갱신(있으면 갱신, 없으면 추가)
    CanvasMap[name] = canvas;

    // 목록에 추가
    Canvases.push_back(canvas);
}

void Scene::RemoveCanvas(const std::shared_ptr<GameObject>& canvas)
{
    if (!canvas) return;

    auto it = std::find_if(Canvases.begin(), Canvases.end(), [&](const std::weak_ptr<GameObject>& c)
    {
        return !c.expired() && c.lock() == canvas;
    });

    if (it != Canvases.end())
    {
        auto canvasCom = canvas->GetComponent<Canvas>();
        for (auto& uiObj : canvasCom->UIObjs)
        {
            if (auto uiObjPtr = uiObj.lock())
                uiObjPtr->Destroy();
        }
        canvasCom->UIObjs.clear();

        std::erase_if(Canvases, [&](const std::weak_ptr<GameObject>& c)
        {
            return c.expired() || c.lock() == canvas;
        });

        std::erase_if(CanvasMap, [&](auto& pair)
        {
            auto sp = pair.second.lock();
            return !sp || sp == canvas;
        });

        canvas->Destroy();
    }
}

std::shared_ptr<GameObject> Scene::FindCanvasName(std::string_view name)
{
    if (CanvasMap.find(name.data()) != CanvasMap.end())
    {
        return CanvasMap[name.data()].lock();
    }
    return nullptr;
}

std::shared_ptr<GameObject> Scene::FindCanvasIndex(size_t index)
{
    if (index < Canvases.size())
    {
        return Canvases[index].lock();
    }
    return nullptr;
}


