#pragma once
#include "LightProperty.h"
#include "PhysicsManager.h"
#include "AssetBundle.h"
#include "Scene.generated.h"
#include "EBodyType.h"
#include <unordered_map>

class GameObject;
class RenderScene;
class SceneManager;
class LightComponent;
class MeshRenderer;
struct ICollider;
class Texture;
class RigidBodyComponent;
class TerrainComponent;
class FoliageComponent;
class ImageComponent;
class TextComponent;
class ReferenceAssets;
class BoxColliderComponent;
class SphereColliderComponent;
class CapsuleColliderComponent;
class MeshColliderComponent;
class CharacterControllerComponent;
class TerrainColliderComponent;
class Transform;
class Animator;
class Scene
{
public:
   ReflectScene
    [[Serializable]]
	Scene();
	~Scene();

    [[Property]]
	std::vector<std::shared_ptr<GameObject>> m_SceneObjects;

	std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);
	std::shared_ptr<GameObject> CreateGameObject(std::string_view name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = -1);
	std::shared_ptr<GameObject> LoadGameObject(size_t instanceID, std::string_view name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = -1);
	std::shared_ptr<GameObject> GetGameObject(GameObject::Index index);
    std::shared_ptr<GameObject> TryGetGameObject(GameObject::Index index);
    // Detach a GameObject subtree from this scene for DontDestroyOnLoad rebind
    void DetachGameObjectHierarchy(GameObject* root);
    // === C안: 공식 경로로 기존 객체(DDOL)를 이 씬에 부착 ===
    // 단일 객체를 붙임(부모 인덱스는 이 씬 기준). 유니크 네임/Tag/Layer/루트 children/Transform 부모까지 처리.
    GameObject::Index AttachExistingGameObject(std::shared_ptr<GameObject> go, GameObject::Index parentIndex);
    // DDOL 서브트리를 한꺼번에 붙임. parent/child 인덱스는 go들이 원래 갖고 있던 서브트리 상대관계를 따름.
    // 반환: oldIndex -> newIndex 매핑(이 씬 기준)
    std::unordered_map<GameObject::Index, GameObject::Index>
        AttachExistingGameObjectHierarchy(const std::vector<std::shared_ptr<GameObject>>& roots);
    std::shared_ptr<GameObject> GetGameObject(std::string_view name);
    const std::vector<GameObject*>& GetSelectedSceneObjects() const { return m_selectedSceneObjects; }
	void AddSelectedSceneObject(GameObject* sceneObject);
	void RemoveSelectedSceneObject(GameObject* sceneObject);
	void ClearSelectedSceneObjects();
	void AddRootGameObject(std::string_view name);
	void DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject);
	void DestroyGameObject(GameObject::Index index);

    std::vector<std::shared_ptr<GameObject>> CreateGameObjects(size_t createSize, GameObject::Index parentIndex = -1);

	inline void InsertGameObjects(std::vector<std::shared_ptr<GameObject>>& gameObjects)
	{
		m_SceneObjects.insert(m_SceneObjects.end(), gameObjects.begin(), gameObjects.end());
	}

private:
    friend class SceneManager;
    //for Editor
    void Reset();

    // 이름 충돌 방지
    std::string MakeUniqueName(std::string_view base);

public:
    //Events
    //Initialization
    Core::Delegate<void>					AwakeEvent{};
    Core::Delegate<void>					OnEnableEvent{};
    Core::Delegate<void>					StartEvent{};

    //Physics
    Core::Delegate<void, float>				FixedUpdateEvent{};
	Core::Delegate<void, float>				InternalPhysicsUpdateEvent{};
    Core::Delegate<void, const Collision&>	OnTriggerEnterEvent{};
    Core::Delegate<void, const Collision&>	OnTriggerStayEvent{};
    Core::Delegate<void, const Collision&>	OnTriggerExitEvent{};
    Core::Delegate<void, const Collision&>	OnCollisionEnterEvent{};
	Core::Delegate<void, const Collision&>	OnCollisionStayEvent{};
	Core::Delegate<void, const Collision&>	OnCollisionExitEvent{};

    //Game logic
    Core::Delegate<void, float>				UpdateEvent{};
    Core::Delegate<void, float>				LateUpdateEvent{};

    //Disable or Enable
    Core::Delegate<void>					OnDisableEvent{};
    Core::Delegate<void>					OnDestroyEvent{};

public:
    //EventBroadcaster
    //Initialization
    void Awake();
    void OnEnable();
    void Start();

    //Physics
    void FixedUpdate(float deltaSecond);
    void OnTriggerEnter(const Collision& collider);
    void OnTriggerStay(const Collision& collider);
    void OnTriggerExit(const Collision& collider);
    void OnCollisionEnter(const Collision& collider);
    void OnCollisionStay(const Collision& collider);
    void OnCollisionExit(const Collision& collider);

    //Game logic
    void Update(float deltaSecond);
    void YieldNull();
    void LateUpdate(float deltaSecond);

    //Disable or Enable
    void OnDisable();
	void OnDestroy();

    void AllDestroyMark();

	static Scene* CreateNewScene(std::string_view sceneName = "SampleScene")
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = sceneName.data();
		allocScene->AddRootGameObject(sceneName);
		return allocScene;
	}

	static Scene* LoadScene(std::string_view name)
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = name.data();
		return allocScene;
	}

    std::atomic_bool m_isAwake{ false };
    std::atomic_bool m_isLoaded{ false };
    std::atomic_bool m_isDirty{ false };
    std::atomic_bool m_isEnable{ false };
    [[Property]]
    size_t m_buildIndex{ 0 };
    [[Property]]
	HashingString m_sceneName;
    [[Property]]
	AssetBundle m_requiredLoadAssetsBundle{};

public:
    GameObject* GetSelectSceneObject() { return m_selectedSceneObject; }
    void ResetSelectedSceneObject();

public:
	void CollectLightComponent(LightComponent* ptr);
	void UnCollectLightComponent(LightComponent* ptr);
    uint32 UpdateLight(LightProperties& lightProperties) const;
    std::pair<size_t, Light&> AddLight();
	Light& GetLight(size_t index);
    void RemoveLight(size_t index);
	void DestroyLight();

public:
	void CollectMeshRenderer(MeshRenderer* ptr);
	void UnCollectMeshRenderer(MeshRenderer* ptr);
	std::vector<MeshRenderer*>& GetMeshRenderers() { return m_allMeshRenderers; }
	std::vector<MeshRenderer*>& GetSkinnedMeshRenderers() { return m_skinnedMeshRenderers; }
	std::vector<MeshRenderer*>& GetStaticMeshRenderers() { return m_staticMeshRenderers; }

public:
    void CollectTerrainComponent(TerrainComponent* ptr);
    void UnCollectTerrainComponent(TerrainComponent* ptr);
    std::vector<TerrainComponent*>& GetTerrainComponent() { return m_terrainComponents; }

public:
    void CollectFoliageComponent(FoliageComponent* ptr);
    void UnCollectFoliageComponent(FoliageComponent* ptr);
    std::vector<FoliageComponent*>& GetFoliageComponents() { return m_foliageComponents; }

public:
	void CollectRigidBodyComponent(RigidBodyComponent* ptr);
	void UnCollectRigidBodyComponent(RigidBodyComponent* ptr);

	void CollectColliderComponent(BoxColliderComponent* ptr);
	void CollectColliderComponent(SphereColliderComponent* ptr);
	void CollectColliderComponent(CapsuleColliderComponent* ptr);
	void CollectColliderComponent(MeshColliderComponent* ptr);
	void CollectColliderComponent(CharacterControllerComponent* ptr);
	void CollectColliderComponent(TerrainColliderComponent* ptr);

public:
	void UnCollectColliderComponent(BoxColliderComponent* ptr);
	void UnCollectColliderComponent(SphereColliderComponent* ptr);
	void UnCollectColliderComponent(CapsuleColliderComponent* ptr);
	void UnCollectColliderComponent(MeshColliderComponent* ptr);
	void UnCollectColliderComponent(CharacterControllerComponent* ptr);
	void UnCollectColliderComponent(TerrainColliderComponent* ptr);

	std::vector<BoxColliderComponent*>& GetBoxColliderComponents() { return m_boxColliderComponents; }
	std::vector<SphereColliderComponent*>& GetSphereColliderComponents() { return m_sphereColliderComponents; }
	std::vector<CapsuleColliderComponent*>& GetCapsuleColliderComponents() { return m_capsuleColliderComponents; }
	std::vector<MeshColliderComponent*>& GetMeshColliderComponents() { return m_meshColliderComponents; }
	std::vector<CharacterControllerComponent*>& GetCharacterControllerComponents() { return m_characterControllerComponents; }

private:
    void DestroyGameObjects();
	void DestroyComponents();
    std::string GenerateUniqueGameObjectName(const std::string_view& name);
	void RemoveGameObjectName(const std::string_view& name);
    void UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model, bool recursive = false);

private:
	void SetInternalPhysicData();

public:
    void AllUpdateWorldMatrix();

private:
    std::unordered_set<std::string> m_gameObjectNameSet{};
	std::unordered_set<Transform*>	m_globalDirtySet{};
	std::vector<LightComponent*>    m_lightComponents;
	std::vector<MeshRenderer*>      m_allMeshRenderers;
	std::vector<MeshRenderer*>      m_staticMeshRenderers;
	std::vector<MeshRenderer*>      m_skinnedMeshRenderers;
    std::vector<Light>              m_lights;
    std::vector<TerrainComponent*>  m_terrainComponents;
    std::vector<FoliageComponent*>  m_foliageComponents;
	std::mutex						sceneMutex{};

private:
	friend class PhysicsManager;
	using RigidBodyTypeLinkCallback = std::unordered_map<GameObject*, std::function<void(const EBodyType&)>>;
	using ColliderContainerType = std::unordered_map<PhysicsManager::ColliderID, PhysicsManager::ColliderInfo>;

	std::vector<RigidBodyComponent*>            m_rigidBodyComponents;
	std::vector<BoxColliderComponent*>          m_boxColliderComponents;
	std::vector<SphereColliderComponent*>       m_sphereColliderComponents;
	std::vector<CapsuleColliderComponent*>      m_capsuleColliderComponents;
	std::vector<MeshColliderComponent*>         m_meshColliderComponents;
	std::vector<CharacterControllerComponent*>  m_characterControllerComponents;
	std::vector<TerrainColliderComponent*>		m_terrainColliderComponents;
	std::vector<std::shared_ptr<Animator*>>     m_animators;
    RigidBodyTypeLinkCallback					m_ColliderTypeLinkCallback;
	ColliderContainerType						m_colliderContainer;

public:
	HashingString GetSceneName() const { return m_sceneName; }
    std::vector<Texture*>		m_lightmapTextures{};
    std::vector<Texture*>		m_directionalmapTextures{};
    Core::DelegateHandle		resetObjHandle{};
    GameObject*					m_selectedSceneObject = nullptr;
	std::vector<GameObject*>	m_selectedSceneObjects;
};
