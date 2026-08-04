#include "Scene.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "HotLoadSystem.h"
#include "ModuleBehavior.h"
#include "LightComponent.h"
#include "MeshRenderer.h"
#include "Terrain.h"
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
#include "DeviceState.h"
#include "SpriteSheetComponent.h"
#include "CullingManager.h"
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
    // ★ 단계마다 즉시 찍는다. 두 번째 씬의 delete에서 종료가 멈추는 것을
    //   추적 로그로 여기까지 좁혀 왔다.
    std::printf("[SHUTDOWN] ~Scene 진입(오브젝트 %zu)\n", m_SceneObjects.size());
    SceneManagers->resetSelectedObjectEvent -= resetObjHandle;
    std::printf("[SHUTDOWN] ~Scene 이벤트 해제 반환\n");
    std::printf("[SHUTDOWN] ~Scene AwakeEvent\n");
    AwakeEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnEnableEvent\n");
    OnEnableEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene StartEvent\n");
    StartEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene FixedUpdateEvent\n");
    FixedUpdateEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene InternalPhysicsUpdateEvent\n");
    InternalPhysicsUpdateEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnTriggerEnterEvent\n");
    OnTriggerEnterEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnTriggerStayEvent\n");
    OnTriggerStayEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnTriggerExitEvent\n");
    OnTriggerExitEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnCollisionEnterEvent\n");
    OnCollisionEnterEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnCollisionStayEvent\n");
    OnCollisionStayEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnCollisionExitEvent\n");
    OnCollisionExitEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene UpdateEvent\n");
    UpdateEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene LateUpdateEvent\n");
    LateUpdateEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnDisableEvent\n");
    OnDisableEvent.Clear();
    std::printf("[SHUTDOWN] ~Scene OnDestroyEvent\n");
    OnDestroyEvent.Clear();

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
    std::printf("[SHUTDOWN] ~Scene 컨테이너 정리 완료\n");
    m_SceneObjects.clear();
    std::printf("[SHUTDOWN] ~Scene 완료\n");
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

void Scene::CullMeshData()
{
    InternalPauseUpdateForUI();

    std::vector<MeshRenderer*> allMeshes = m_allMeshRenderers;
    std::vector<MeshRenderer*> staticMeshes = m_staticMeshRenderers;
    std::vector<MeshRenderer*> skinnedMeshes = m_skinnedMeshRenderers;
    std::vector<TerrainComponent*> terrainComponents = m_terrainComponents;
    std::vector<FoliageComponent*> foliageComponents = m_foliageComponents;
    std::vector<ImageComponent*> imageComponents = UIManagers->Images;
    std::vector<TextComponent*> textComponents = UIManagers->Texts;
    std::vector<SpriteRenderer*> spriteRenderers = m_spriteRenderers;
    std::vector<SpriteSheetComponent*> spriteSheetComponents = UIManagers->SpriteSheets;
    std::vector<DecalComponent*> decalComponents = m_decalComponents;

    auto renderScene = SceneManagers->GetRenderScene();

    for (auto camera : CameraManagement->GetCameras())
    {
        if (!camera) return;
        if (!RenderPassData::VaildCheck(camera.get())) return;
        auto data = RenderPassData::GetData(camera.get());

        std::vector<std::shared_ptr<MeshRenderer>> visibleMeshes;

		auto frustum = camera->GetFrustum();

		Mathf::xVector camPos = camera->m_eyePosition;

        CullingManagers->FrustumCullFrontToBack(
            camPos,
            frustum,
			visibleMeshes);

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& mesh : allMeshes)
            {
                if (!mesh) continue;
                try
                {
                    renderScene->UpdateCommand(mesh);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating mesh command: " << e.what() << '\n';
                }

                if (mesh->IsDestroyMark() ||
                    false == mesh->IsEnabled() ||
                    false == mesh->GetOwner()->IsEnabled()
                    ) continue;
                data->PushShadowRenderData(mesh->GetInstanceID());
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& culledMesh : staticMeshes)
            {
                if (false == culledMesh->IsEnabled() || false == culledMesh->GetOwner()->IsEnabled()) continue;
                auto frustum = camera->GetFrustum();
                if (frustum.Intersects(culledMesh->GetBoundingBox()))
                {
                    data->PushCullData(culledMesh->GetInstanceID());
                }
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& skinnedMesh : skinnedMeshes)
            {
                if (false == skinnedMesh->IsEnabled() || false == skinnedMesh->GetOwner()->IsEnabled()) continue;

                auto frustum = camera->GetFrustum();
                if (frustum.Intersects(skinnedMesh->GetBoundingBox()))
                {
                    data->PushCullData(skinnedMesh->GetInstanceID());
                }
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& terrainComponent : terrainComponents)
            {
                try
                {
                    renderScene->UpdateCommand(terrainComponent);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating terrain command: " << e.what() << std::endl;
                }

                if (false == terrainComponent->IsEnabled() || false == terrainComponent->GetOwner()->IsEnabled()) continue;

                data->PushCullData(terrainComponent->GetInstanceID());
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& foliageComponent : foliageComponents)
            {
                try
                {
                    renderScene->UpdateCommand(foliageComponent);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating foliage command: " << e.what() << std::endl;
                }

                if (false == foliageComponent->IsEnabled() || false == foliageComponent->GetOwner()->IsEnabled()) continue;

                data->PushCullData(foliageComponent->GetInstanceID());
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& decalComponent : decalComponents)
            {
                try
                {
                    renderScene->UpdateCommand(decalComponent);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating decal command: " << e.what() << std::endl;
                }

                if (false == decalComponent->IsEnabled() || false == decalComponent->GetOwner()->IsEnabled()) continue;

                data->PushCullData(decalComponent->GetInstanceID());
            }
        });

        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& sprite : spriteRenderers)
            {
                try
                {
                    renderScene->UpdateCommand(sprite);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating sprite command: " << e.what() << std::endl;
                }

                if (false == sprite->IsEnabled() || false == sprite->GetOwner()->IsEnabled()) continue;

                data->PushCullData(sprite->GetInstanceID());
            }
        });

		//여기 부터는 UI -> 컬링하는 부분이 아님 분리 필요 -> UI 렌더링 데이터 푸시
        SceneManagers->m_threadPool->Enqueue([=]
        {
            for (auto& image : imageComponents)
            {
                try
                {
                    renderScene->UpdateCommand(image);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating image command: " << e.what() << std::endl;
                }

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
                try
                {
                    renderScene->UpdateCommand(text);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating text command: " << e.what() << std::endl;
                }

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
                try
                {
                    renderScene->UpdateCommand(spriteSheet);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error updating sprite command: " << e.what() << std::endl;
                }

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

                auto moduleBehaviorComponents = obj->GetComponents<ModuleBehavior>();
                for (const auto& moduleBehavior : moduleBehaviorComponents)
                {
                    moduleBehavior->Update(deltaTime);
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
    if (ScriptManager->IsDerty())
    {
        ScriptManager->SetReload(true);
        ScriptManager->ReplaceScriptComponent();
        ScriptManager->DertyFlagClear();
    }
    else
    {
        for (const auto& obj : m_SceneObjects)
        {
            if (!obj) continue;

            auto scripts = obj->GetComponents<ModuleBehavior>();
            for (auto& script : scripts)
            {
                auto name = script->m_name;
                if (script && SceneManagers->m_isGameStart)
                {
                    ScriptManager->BindScriptEvents(script, name.ToString());
                }
            }
        }
    }
}

void Scene::Awake()
{
    AwakeEvent.Broadcast();
}

void Scene::OnEnable()
{
    OnEnableEvent.Broadcast();
}

void Scene::Start()
{
    StartEvent.Broadcast();
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
    FixedUpdateEvent.Broadcast(deltaSecond);
    PROFILE_CPU_END();
    PROFILE_CPU_BEGIN("internalfixedBroadcast");
    InternalPhysicsUpdateEvent.Broadcast(deltaSecond);
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
    // C# 스크립트에도 물리 콜백을 전달한다.
    //
    // 위의 ModuleBehavior 경로와 달리 즉시 호출하지 않고 큐에만 담는다 —
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
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnTriggerEnterEvent.TargetInvoke(
            t->m_onTriggerEnterEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerEnter);
}

void Scene::OnTriggerStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnTriggerStayEvent.TargetInvoke(
            t->m_onTriggerStayEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerStay);
}

void Scene::OnTriggerExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnTriggerExitEvent.TargetInvoke(
            t->m_onTriggerExitEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::TriggerExit);
}

void Scene::OnCollisionEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnCollisionEnterEvent.TargetInvoke(
            t->m_onCollisionEnterEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionEnter);
}

void Scene::OnCollisionStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnCollisionStayEvent.TargetInvoke(
            t->m_onCollisionStayEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionStay);
}

void Scene::OnCollisionExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
    for (auto& t : target) {
        OnCollisionExitEvent.TargetInvoke(
            t->m_onCollisionExitEventHandle, collider);
    }

    QueueManagedCollision(collider, ClrHost::PhysicsEventKind::CollisionExit);
}

void Scene::Update(float deltaSecond)
{
    PROFILE_CPU_BEGIN("PreAllUpdateWorldMatrix");
    AllUpdateWorldMatrix();
    PROFILE_CPU_END();

    PROFILE_CPU_BEGIN("UpdateEvent");
    UpdateEvent.Broadcast(deltaSecond);
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
    LateUpdateEvent.Broadcast(deltaSecond);

    CullMeshData();
}

void Scene::OnDisable()
{
    PROFILE_CPU_BEGIN("OnDisable");
    OnDisableEvent.Broadcast();
    PROFILE_CPU_END();
}

void Scene::OnDestroy()
{
    PROFILE_CPU_BEGIN("OnDestroyBroadcast");
    OnDestroyEvent.Broadcast();
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

uint32 Scene::UpdateLight(LightProperties& lightProperties) const
{
    memset(lightProperties.m_lights, 0, sizeof(Light) * MAX_LIGHTS);

    uint32 count{};
    for (int i = 0; i < static_cast<int>(m_lights.size()); ++i)
    {
        if (LightStatus::Disabled != m_lights[i].m_lightStatus)
        {
            lightProperties.m_lights[count++] = m_lights[i];
        }
    }

    return count;
}

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

                auto behavior = std::dynamic_pointer_cast<ModuleBehavior>(component);
                if (behavior)
                {
                    obj->RemoveScriptComponent(behavior.get());
                }
                else
                {
                    obj->RemoveComponentTypeID(component->GetTypeID());
                }

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


