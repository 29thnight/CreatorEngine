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
    m_pendingAwake.clear();
    m_pendingStart.clear();
    m_updateList.clear();
    m_lateUpdateList.clear();
    m_fixedUpdateList.clear();
    m_destroyWatchList.clear();

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
}

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    std::string uniqueName = GenerateUniqueGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);
    sceneObject->m_ownerScene = this;
    sceneObject->m_transform.SetDirty();

    m_SceneObjects.push_back(sceneObject);

    const_cast<GameObject::Index&>(sceneObject->m_index) = static_cast<GameObject::Index>(m_SceneObjects.size() - 1);

    m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

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

    GameObject::Index index = static_cast<GameObject::Index>(m_SceneObjects.size());
    auto ptr = shared_alloc<GameObject>(this, uniqueName, GameObjectType::Empty, index, -1);
    if (nullptr == ptr)
    {
        return;
    }

    m_SceneObjects.push_back(ptr);
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

    GameObject::Index index = static_cast<GameObject::Index>(m_SceneObjects.size());

    auto ptr = shared_alloc<GameObject>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        return nullptr;
    }
    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    m_SceneObjects.push_back(ptr);
    auto parentObj = GetGameObject(parentIndex);
    if (parentObj->m_index != index)
    {
        parentObj->m_childrenIndices.push_back(index);
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
        parentIndex = 0;
    }

    std::string uniqueName = GenerateUniqueGameObjectName(name);

    GameObject::Index index = static_cast<GameObject::Index>(m_SceneObjects.size());
    auto ptr = shared_alloc<GameObject>(this, uniqueName, type, index, parentIndex);
    if (nullptr == ptr)
    {
        return nullptr;
    }

    ptr->m_ownerScene = this;
    ptr->m_removedSuffixNumberTag = name.data();

    m_SceneObjects.push_back(ptr);

    return m_SceneObjects[index];
}

std::shared_ptr<GameObject> Scene::GetGameObject(GameObject::Index index)
{
    if (index < m_SceneObjects.size())
    {
        return m_SceneObjects[index];
    }

    if (!m_SceneObjects.empty())
    {
        return m_SceneObjects[0];
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

    // breadth-first (인덱스 재배열 없이 안전하게 순회)
    std::vector<GameObject::Index> queue;
    queue.push_back(root->m_index);

    // 루트부터 부모/씬 루트 children 에서 분리
    auto detachFromParent = [&](GameObject* node) {
        if (!node) return;
        // 부모 children에서 제거
        if (GameObject::IsValidIndex(node->m_parentIndex))
        {
            if (auto parent = TryGetGameObject(node->m_parentIndex))
            {
                std::erase(parent->m_childrenIndices, node->m_index);
            }
        }
        // 씬 루트 children에서 제거
        if (!m_SceneObjects.empty() && m_SceneObjects[0])
        {
            std::erase(m_SceneObjects[0]->m_childrenIndices, node->m_index);
        }
        };
    detachFromParent(root);

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
        node->m_transform.SetParentID(GameObject::INVALID_INDEX);
        node->m_parentIndex = GameObject::INVALID_INDEX;

        // 원 씬 소유 컨테이너에서 tombstone(null) 처리 → 중복 소유/순회 방지
        if (static_cast<size_t>(idx) < m_SceneObjects.size())
        {
            m_SceneObjects[idx].reset();
        }
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

    // 새 인덱스 할당
    GameObject::Index newIndex = static_cast<GameObject::Index>(m_SceneObjects.size());
    go->m_index = newIndex;
    m_SceneObjects.push_back(go);

    // Tag/Layer 재등록
    if (!go->m_tag.ToString().empty())
        TagManager::GetInstance()->AddTagToObject(go->m_tag.ToString(), go.get());
    if (!go->m_layer.ToString().empty())
        TagManager::GetInstance()->AddObjectToLayer(go->m_layer.ToString(), go.get());

    // Transform 부모 세팅 (루트 규약: INVALID_INDEX == 루트)
    if (GameObject::IsValidIndex(parentIndex))
    {
        go->m_parentIndex = parentIndex;
        if (auto parent = TryGetGameObject(parentIndex))
        {
            if (std::ranges::find(parent->m_childrenIndices, newIndex) == parent->m_childrenIndices.end())
                parent->m_childrenIndices.push_back(newIndex);
        }
        go->m_transform.SetParentID(parentIndex);
    }
    else
    {
        go->m_parentIndex = GameObject::INVALID_INDEX;
        go->m_transform.SetParentID(GameObject::INVALID_INDEX);
        // 씬 루트 children 연결
        if (!m_SceneObjects.empty() && m_SceneObjects[0])
        {
            auto& rootChildren = m_SceneObjects[0]->m_childrenIndices;
            if (std::ranges::find(rootChildren, newIndex) == rootChildren.end())
                rootChildren.push_back(newIndex);
        }
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

void Scene::UpdateRenderData()
{
    InternalPauseUpdateForUI();

    std::vector<MeshRenderer*> allMeshes = m_allMeshRenderers;
    std::vector<TerrainComponent*> terrainComponents = m_terrainComponents;
    std::vector<FoliageComponent*> foliageComponents = m_foliageComponents;
    std::vector<ImageComponent*> imageComponents = UIManagers->Images;
    std::vector<TextComponent*> textComponents = UIManagers->Texts;
    std::vector<SpriteRenderer*> spriteRenderers = m_spriteRenderers;
    std::vector<SpriteSheetComponent*> spriteSheetComponents = UIManagers->SpriteSheets;
    std::vector<DecalComponent*> decalComponents = m_decalComponents;

    auto renderScene = SceneManagers->GetRenderScene();

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
    if (nullptr != renderScene)
    {
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
        SceneManagers->m_threadPool->Enqueue([=]
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

        SceneManagers->m_threadPool->Enqueue([=]
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

        SceneManagers->m_threadPool->Enqueue([=]
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

        SceneManagers->m_threadPool->NotifyAllAndWait();
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
                    textComponent->Update(deltaTime);
                }

                auto spriteSheetComponents = obj->GetComponents<SpriteSheetComponent>();
                for (const auto& spriteSheetComponent : spriteSheetComponents)
                {
                    spriteSheetComponent->Update(deltaTime);
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
    // swap-and-pop 제거. 순서를 보존하지 않는 것이 의도다 —
    // 보존해야 하는 것은 단계 사이의 순서이지 같은 단계 안의 순서가 아니고,
    // erase로 앞당기면 제거가 O(n)이 되어 씬 전환마다 수천 번 돈다.
    bool RemoveFromList(std::vector<Component*>& list, Component* target)
    {
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i] != target) continue;
            list[i] = list.back();
            list.pop_back();
            return true;
        }
        return false;
    }

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

    // 이미 Awake를 받은 컴포넌트는 큐에 넣지 않는다.
    //
    // 등록은 한 번이 아니다 — DDOL은 씬을 건널 때마다, 경로 전환은 그 시점에
    // 다시 등록한다. 상태를 보지 않으면 그때마다 Awake가 또 돈다.
    if ((mask & Lifecycle::Bit_Awake) && !component->HasLifecycleState(Component::State_AwakeCalled))
    {
        m_pendingAwake.push_back(component);
    }
    else if ((mask & Lifecycle::Bit_Start) && !component->HasLifecycleState(Component::State_StartCalled))
    {
        m_pendingStart.push_back(component);
    }

    if (mask & Lifecycle::Bit_Update)      m_updateList.push_back(component);
    if (mask & Lifecycle::Bit_LateUpdate)  m_lateUpdateList.push_back(component);
    if (mask & Lifecycle::Bit_FixedUpdate) m_fixedUpdateList.push_back(component);
    if (mask & Lifecycle::Bit_OnDestroy)   m_destroyWatchList.push_back(component);
}

void Scene::UnregisterComponent(Component* component)
{
    if (nullptr == component) return;

    RemoveFromList(m_pendingAwake, component);
    RemoveFromList(m_pendingStart, component);
    RemoveFromList(m_updateList, component);
    RemoveFromList(m_lateUpdateList, component);
    RemoveFromList(m_fixedUpdateList, component);
    RemoveFromList(m_destroyWatchList, component);
}

Scene::RegistryCounts Scene::GetRegistryCounts() const
{
    return RegistryCounts{
        m_pendingAwake.size(), m_pendingStart.size(),
        m_updateList.size(), m_lateUpdateList.size(), m_fixedUpdateList.size(),
    };
}

void Scene::RegistryDrainAwakeAndStart()
{
    // 큐를 통째로 옮겨 놓고 돈다.
    //
    // Awake 안에서 AddComponent를 부르면 새 컴포넌트가 m_pendingAwake에 들어오는데,
    // 원본을 순회 중이면 그 push_back이 순회를 무효화한다. 옮겨 두면 새로 들어온
    // 것은 이번 바퀴에 끼지 않고 다음 프레임에 처리된다 — 이것이 재진입 안전의
    // 핵심이고, 델리게이트 경로가 하지 못하던 것이다.
    std::vector<Component*> awaking;
    awaking.swap(m_pendingAwake);

    for (Component* component : awaking)
    {
        if (nullptr == component) continue;
        GameObject* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled()) { m_pendingAwake.push_back(component); continue; }

        component->MarkLifecycleState(Component::State_AwakeCalled);
        LIFECYCLE_TRACE(Lifecycle::Phase::Awake, TraceTypeName(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->Awake();

        const uint16_t mask = Lifecycle::Registry::Find(component->GetTypeID().m_ID_Data);
        if ((mask & Lifecycle::Bit_Start) && !component->HasLifecycleState(Component::State_StartCalled))
        {
            m_pendingStart.push_back(component);
        }
    }

    std::vector<Component*> starting;
    starting.swap(m_pendingStart);

    for (Component* component : starting)
    {
        if (nullptr == component) continue;
        GameObject* owner = component->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!component->IsEnabled()) { m_pendingStart.push_back(component); continue; }

        component->MarkLifecycleState(Component::State_StartCalled);
        LIFECYCLE_TRACE(Lifecycle::Phase::Start, TraceTypeName(component),
            owner->m_name.ToString().c_str(), component->GetInstanceID());
        component->Start();
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
        + " (update 리스트 " + std::to_string(m_updateList.size())
        + " · pendingAwake " + std::to_string(m_pendingAwake.size()) + ")");
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
    if (m_destroyWatchList.empty()) return;

    std::vector<Component*> doomed;
    for (Component* component : m_destroyWatchList)
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

        LIFECYCLE_TRACE(Lifecycle::Phase::OnDestroy, TraceTypeName(component), ownerName.c_str(), component->GetInstanceID());
        component->OnDestroy();

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
    RegistryTick(m_fixedUpdateList, Lifecycle::Bit_FixedUpdate, deltaSecond);
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("internalfixedBroadcast");
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
    RegistryTick(m_updateList, Lifecycle::Bit_Update, deltaSecond);
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
    RegistryTick(m_lateUpdateList, Lifecycle::Bit_LateUpdate, deltaSecond);

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

    for (auto& obj : m_SceneObjects)
    {
        if (obj && deletedIndices.contains(obj->m_index))
        {
            for (auto childIdx : obj->m_childrenIndices)
            {
                if (GameObject::IsValidIndex(childIdx) &&
                    childIdx < m_SceneObjects.size() &&
                    m_SceneObjects[childIdx])
                {
                    m_SceneObjects[childIdx]->m_parentIndex = GameObject::INVALID_INDEX;
                }
            }

            obj->m_childrenIndices.clear();
            obj.reset();
        }
    }

    std::erase_if(m_SceneObjects, [](const auto& obj) { return obj == nullptr; });

    std::unordered_map<uint32_t, uint32_t> indexMap;
    for (uint32_t i = 0; i < m_SceneObjects.size(); ++i)
    {
        indexMap[m_SceneObjects[i]->m_index] = i;
    }

    for (auto& obj : m_SceneObjects)
    {
        uint32_t oldIndex = obj->m_index;

        if (indexMap.contains(obj->m_parentIndex))
        {
            obj->m_parentIndex = indexMap[obj->m_parentIndex];
            obj->m_rootIndex = indexMap[obj->m_rootIndex];
            obj->m_transform.SetParentID(obj->m_parentIndex);
        }
        else
        {
            obj->m_parentIndex = GameObject::INVALID_INDEX;
        }

        for (auto& childIndex : obj->m_childrenIndices)
        {
            if (indexMap.contains(childIndex))
                childIndex = indexMap[childIndex];
            else
                childIndex = GameObject::INVALID_INDEX;
        }

        std::erase_if(obj->m_childrenIndices, GameObject::IsInvalidIndex);

        obj->m_index = indexMap[oldIndex];
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

void Scene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model, bool /*recursive*/)
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
        obj->m_transform.SetAndDecomposeMatrix(XMMatrixMultiply(bone ?
            animator->m_localTransforms[bone->m_index] : obj->m_transform.GetLocalMatrix(), model));
        break;
    }
    default:
    {
        if (obj->m_transform.IsDirty())
        {
            auto renderer = obj->GetComponent<MeshRenderer>();
            if (renderer)
            {
                renderer->SetNeedUpdateCulling(true);
            }
        }
        model = XMMatrixMultiply(obj->m_transform.GetLocalMatrix(), model);
        obj->m_transform.SetAndDecomposeMatrix(model);
        break;
    }
    }

    for (auto& childIndex : obj->m_childrenIndices)
    {
        if (childIndex == obj->m_index) continue;
        UpdateModelRecursive(childIndex, model, true);
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
    // 계층에 순환이 있어도 여기서 멈춘다.
    if (!visited.insert(obj).second) return;

    // 방문 집합이 순환을 막지만, 깊이가 비정상적으로 깊어지는 것 자체가 신호다.
    constexpr int kMaxDepth = 64;
    if (depth > kMaxDepth)
    {
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            Debug->LogError("[UI] 레이아웃 순회가 최대 깊이를 넘었다 — 계층이 지나치게 깊거나 순환한다: "
                + obj->m_name.ToString());
        }
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
        childScale = canvas->ComputeScaleFactor(screenRect);
        childChanged = rect->DriveAsCanvasRoot(screenRect, childScale);
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


