#include "Scene.h"
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
#include "TerrainCollider.h"
#include "RigidBodyComponent.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "TagManager.h"
#include "UIManager.h"
#include "RectTransformComponent.h"
#include <execution>
#include <queue>
#include <algorithm>
#include <queue>

#include "Profiler.h"
Scene::Scene()
{
	resetObjHandle = SceneManagers->resetSelectedObjectEvent.AddRaw(this, &Scene::ResetSelectedSceneObject);
	m_SceneObjects.reserve(3000);
}

Scene::~Scene()
{
    SceneManagers->resetSelectedObjectEvent -= resetObjHandle;
}

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
    std::string uniqueName = GenerateUniqueGameObjectName(sceneObject->GetHashedName().ToString());

    sceneObject->SetName(uniqueName);
	sceneObject->m_ownerScene = this;
	sceneObject->m_transform.SetDirty();

	m_SceneObjects.push_back(sceneObject);

	const_cast<GameObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

	if (!sceneObject->m_tag.ToString().empty())
	{
		TagManager::GetInstance()->AddTagToObject(sceneObject->m_tag.ToString(), sceneObject.get());
	}

	if (!sceneObject->m_layer.ToString().empty())
	{
		TagManager::GetInstance()->AddObjectToLayer(sceneObject->m_layer.ToString(), sceneObject.get());
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

	GameObject::Index index = m_SceneObjects.size();
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

	GameObject::Index index = m_SceneObjects.size();


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


    GameObject::Index index = m_SceneObjects.size();
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
	return m_SceneObjects[0];
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
    auto detachFromParent = [&](GameObject* node){
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

        // 자식 enqueue
        for (auto childIdx : node->m_childrenIndices)
        {
            if (GameObject::IsValidIndex(childIdx))
                queue.push_back(childIdx);
        }

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
std::string Scene::MakeUniqueName(std::string_view base) const
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
            if (std::find(parent->m_childrenIndices.begin(), parent->m_childrenIndices.end(), newIndex) == parent->m_childrenIndices.end())
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
            if (std::find(rootChildren.begin(), rootChildren.end(), newIndex) == rootChildren.end())
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
    ordered.reserve(roots.size()*4);
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
		if (obj->GetHashedName() == hashedName)
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

std::vector<std::shared_ptr<GameObject>> Scene::CreateGameObjects(size_t createSize, GameObject::Index parentIndex)
{
	std::vector<std::shared_ptr<GameObject>> createObjects{ createSize };
	for (int i = 0; i < createSize; ++i)
	{
		auto newObject = CreateGameObject("default", GameObjectType::Empty, parentIndex);
		if (newObject)
		{
			createObjects.push_back(newObject);
		}
	}

	return createObjects;
}

void Scene::Reset()
{
    ScriptManager->SetReload(true);
    ScriptManager->ReplaceScriptComponent();
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
#ifndef BUILD_FLAG
	PROFILE_CPU_BEGIN("AllUpdateWorldMatrix");
	AllUpdateWorldMatrix();	// render 단계에서 imgui를 통해 transform의 변경이 있으므로 디버그모드에서만 사용.
	PROFILE_CPU_END();
#endif // BUILD_FLAG

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

void Scene::OnTriggerEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
		OnTriggerEnterEvent.TargetInvoke(
			t->m_onTriggerEnterEventHandle, collider);
	}
}

void Scene::OnTriggerStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnTriggerStayEvent.TargetInvoke(
            t->m_onTriggerStayEventHandle, collider);
    }
}

void Scene::OnTriggerExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnTriggerExitEvent.TargetInvoke(
            t->m_onTriggerExitEventHandle, collider);
    }
}

void Scene::OnCollisionEnter(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionEnterEvent.TargetInvoke(
            t->m_onCollisionEnterEventHandle, collider);
    }
}

void Scene::OnCollisionStay(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionStayEvent.TargetInvoke(
            t->m_onCollisionStayEventHandle, collider);
    }
}

void Scene::OnCollisionExit(const Collision& collider)
{
    auto target = collider.thisObj->GetComponents<ModuleBehavior>();
	for (auto& t : target) {
        OnCollisionExitEvent.TargetInvoke(
            t->m_onCollisionExitEventHandle, collider);
    }
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

	std::vector<MeshRenderer*> allMeshes = m_allMeshRenderers;
	std::vector<MeshRenderer*> staticMeshes = m_staticMeshRenderers;
	std::vector<MeshRenderer*> skinnedMeshes = m_skinnedMeshRenderers;
	std::vector<TerrainComponent*> terrainComponents = m_terrainComponents;
	std::vector<FoliageComponent*> foliageComponents = m_foliageComponents;
	std::vector<ImageComponent*> imageComponents = UIManagers->Images;
	std::vector<TextComponent*> textComponents = UIManagers->Texts;

	for (auto camera : CameraManagement->GetCameras())
	{
		if (!RenderPassData::VaildCheck(camera)) return;
		auto data = RenderPassData::GetData(camera);

		SceneManagers->m_threadPool->Enqueue([=]
		{
			for (auto& mesh : allMeshes)
			{
				if (false == mesh->IsEnabled() || false == mesh->GetOwner()->IsEnabled()) continue;
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
			for (auto& image : imageComponents)
			{
				if (false == image->IsEnabled() || false == image->GetOwner()->IsEnabled()) continue;
				
				auto owner = image->GetOwner();
				if (nullptr == owner) continue;

                                auto scene = owner->GetScene();

                                if (scene && (scene == this || owner->IsDontDestroyOnLoad()))
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

                                if (scene && (scene == this || owner->IsDontDestroyOnLoad()))
                                {
                                        data->PushUIRenderData(text->GetInstanceID());
                                }
			}
		});

		SceneManagers->m_threadPool->NotifyAllAndWait();
	}
}

void Scene::OnDisable()
{
    OnDisableEvent.Broadcast();
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
}

void Scene::AllDestroyMark()
{
    for (const auto& obj : m_SceneObjects)
    {
        if (obj && !obj->IsDestroyMark())
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

	if (std::find(m_selectedSceneObjects.begin(), m_selectedSceneObjects.end(), sceneObject) == m_selectedSceneObjects.end())
	{
		m_selectedSceneObjects.push_back(sceneObject);
		m_selectedSceneObject = sceneObject;
	}
	
}

void Scene::RemoveSelectedSceneObject(GameObject* sceneObject)
{
	if (!sceneObject) return;
	auto it = std::find(m_selectedSceneObjects.begin(), m_selectedSceneObjects.end(), sceneObject);
	if (it != m_selectedSceneObjects.end())
	{
		m_selectedSceneObjects.erase(it);
		if (m_selectedSceneObject == sceneObject)
		{
			m_selectedSceneObject = m_selectedSceneObjects.back();
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
	if (ptr)
	{
		auto it = std::find_if(
			m_lightComponents.begin(),
			m_lightComponents.end(), 
			[ptr](const auto& light)
			{
				return light == ptr;
			});

		if (it == m_lightComponents.end())
		{
			m_lightComponents.push_back(ptr);
		}
	}
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
	for (int i = 0; i < m_lights.size(); ++i)
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
	if(index > m_lights.size() || 0 == m_lights.size())
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

		int&  lightIndex = comp->m_lightIndex;
		auto& lightType  = comp->m_lightType;
        if (auto it = indexRemap.find(lightIndex); it != indexRemap.end())
        {
            lightIndex = static_cast<int>(it->second);
        }

        if (!isFirstDirectional && lightType == LightType::DirectionalLight)
        {
            isFirstDirectional  = true;
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

void Scene::CollectRigidBodyComponent(RigidBodyComponent* ptr)
{
	if (ptr)
	{
		auto it = std::find_if(
			m_rigidBodyComponents.begin(),
			m_rigidBodyComponents.end(),
			[ptr](const auto& body) { return body == ptr; });
		if (it == m_rigidBodyComponents.end())
		{
			m_rigidBodyComponents.push_back(ptr);
		}
	}
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
		auto it = std::find_if(
			m_boxColliderComponents.begin(),
			m_boxColliderComponents.end(),
			[ptr](const auto& box) { return box == ptr; });
		if (it == m_boxColliderComponents.end())
		{
			m_boxColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto boxInfo = ptr->GetBoxInfo();
			auto colliderID = boxInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(boxInfo, ptr->GetColliderType()); 
				//콜라이더 정보 저장
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
				//콜라이더 정보 저장
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
		auto it = std::find_if(
			m_sphereColliderComponents.begin(),
			m_sphereColliderComponents.end(),
			[ptr](const auto& sphere) { return sphere == ptr; });
		if (it == m_sphereColliderComponents.end())
		{
			m_sphereColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto sphereInfo = ptr->GetSphereInfo();
			auto colliderID = sphereInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(sphereInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
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
				Physics->CreateDynamicBody(sphereInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
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
		auto it = std::find_if(
			m_capsuleColliderComponents.begin(),
			m_capsuleColliderComponents.end(),
			[ptr](const auto& capsule) { return capsule == ptr; });
		if (it == m_capsuleColliderComponents.end())
		{
			m_capsuleColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto capsuleInfo = ptr->GetCapsuleInfo();
			auto colliderID = capsuleInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(capsuleInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
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
				Physics->CreateDynamicBody(capsuleInfo, ptr->GetColliderType(), isKinematic);
				//콜라이더 정보 저장
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
		auto it = std::find_if(
			m_meshColliderComponents.begin(),
			m_meshColliderComponents.end(),
			[ptr](const auto& mesh) { return mesh == ptr; });
		if (it == m_meshColliderComponents.end())
		{
			m_meshColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto callback = [=](const EBodyType& bodyType)
		{
			if (nullptr == ptr) return;

			auto convexMeshInfo = ptr->GetMeshInfo();
			auto colliderID = convexMeshInfo.colliderInfo.id;

			if (bodyType == EBodyType::STATIC)
			{
				//pxScene에 엑터 추가
				Physics->CreateStaticBody(convexMeshInfo, ptr->GetColliderType());
				//콜라이더 정보 저장
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
				//콜라이더 정보 저장
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
		auto it = std::find_if(
			m_characterControllerComponents.begin(),
			m_characterControllerComponents.end(),
			[ptr](const auto& character) { return character == ptr; });
		if (it == m_characterControllerComponents.end())
		{
			m_characterControllerComponents.push_back(ptr);
		}

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
		auto it = std::find_if(
			m_terrainColliderComponents.begin(),
			m_terrainColliderComponents.end(),
			[ptr](const auto& terrain) { return terrain == ptr; });
		if (it == m_terrainColliderComponents.end())
		{
			m_terrainColliderComponents.push_back(ptr);
		}

		PhysicsManagers->AddCollider(ptr);

		auto gameObject = ptr->GetOwner();
		auto heightFieldInfo = ptr->GetHeightFieldColliderInfo();
		auto colliderID = heightFieldInfo.colliderInfo.id;

		m_colliderContainer.insert({ colliderID, {
			m_heightFieldTypeId,
			ptr, // corrected variable name from 'collider' to 'ptr'
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
    std::erase_if(m_SceneObjects, [](const auto& obj)
    {
            return obj && obj->IsDontDestroyOnLoad();
    });

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

void Scene::UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model, bool recursive)
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
		const auto& RectTransform = obj->GetComponent<RectTransformComponent>();
		const auto& objParent = m_SceneObjects[obj->m_parentIndex];
		if (!RectTransform || !RectTransform->IsEnabled() || !objParent)
		{
			return;
		}
		const auto& ParentRectTransform = objParent->GetComponent<RectTransformComponent>();
		if (!ParentRectTransform || !ParentRectTransform->IsEnabled())
		{
			return;
		}
		RectTransform->UpdateLayout(ParentRectTransform->GetWorldRect());
		break;
	}
	case GameObjectType::Bone:
	{
		//const auto& animator = GetGameObject(obj->m_rootIndex)->GetComponent<Animator>();
		//if (!animator || !animator->m_Skeleton || !animator->IsEnabled())
		//{
		//	return;
		//}
		//const auto bone = animator->m_Skeleton->FindBone(obj->RemoveSuffixNumberTag());
		//obj->m_transform.SetAndDecomposeMatrix(XMMatrixMultiply(bone ?
		//	animator->m_localTransforms[bone->m_index] : obj->m_transform.GetLocalMatrix(), model));
		//break;

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
		UpdateModelRecursive(childIndex, model, recursive);
	}
}

void Scene::SetInternalPhysicData()
{
	std::erase_if(m_colliderContainer,
		[&](const auto& pair)
		{
			return pair.second.bIsDestroyed == true;
		});

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
			if(iter != m_ColliderTypeLinkCallback.end())
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
			if(iter != m_ColliderTypeLinkCallback.end())
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
			if(iter != m_ColliderTypeLinkCallback.end())
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
			if(iter != m_ColliderTypeLinkCallback.end())
			{
				iter->second(m_bodyType[gameObject]);
			}
			linkCompleteSet.insert(gameObject);
		}
	}

	std::erase_if(m_ColliderTypeLinkCallback, 
		[&linkCompleteSet](const auto& pair)
		{
			return linkCompleteSet.contains(pair.first);
		});
}

void Scene::AllUpdateWorldMatrix()
{
	auto& rootObjects = m_SceneObjects[0]->m_childrenIndices;

	auto updateFunc = [this](GameObject::Index index)
	{
		UpdateModelRecursive(index, XMMatrixIdentity());
	};

	std::for_each(std::execution::par_unseq, rootObjects.begin(), rootObjects.end(), updateFunc);
}