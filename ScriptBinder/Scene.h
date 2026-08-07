#pragma once
#include "LightProperty.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "PhysicsManager.h"
#include "AssetBundle.h"
#include "Scene.generated.h"
#include "EBodyType.h"
#include <unordered_map>

#pragma region forward_decl
struct ICollider;
class GameObject;
class RenderScene;
class SceneManager;
class LightComponent;
class MeshRenderer;
class Texture;
class RigidBodyComponent;
class TerrainComponent;
class FoliageComponent;
class ImageComponent;
class TextComponent;
class DecalComponent;
class ReferenceAssets;
class BoxColliderComponent;
class SphereColliderComponent;
class CapsuleColliderComponent;
class MeshColliderComponent;
class CharacterControllerComponent;
class TerrainColliderComponent;
class Transform;
class Animator;
class SpriteRenderer;
#pragma endregion forward_decl
class Scene
{
public:
   ReflectScene
    [[Serializable]]
	Scene();
	~Scene();

    [[Property]]
	std::vector<std::shared_ptr<GameObject>> m_SceneObjects;
	std::future<void> m_AIFuture;

	std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);
	std::shared_ptr<GameObject> CreateGameObject(std::string_view name, GameObjectType type = GameObjectType::Empty, GameObjectIndex parentIndex = -1);
	std::shared_ptr<GameObject> LoadGameObject(size_t instanceID, std::string_view name, GameObjectType type = GameObjectType::Empty, GameObjectIndex parentIndex = -1);
	std::shared_ptr<GameObject> GetGameObject(GameObjectIndex index);
    std::shared_ptr<GameObject> TryGetGameObject(GameObjectIndex index);
    // Detach a GameObject subtree from this scene for DontDestroyOnLoad rebind
    void DetachGameObjectHierarchy(GameObject* root);
    // === C안: 공식 경로로 기존 객체(DDOL)를 이 씬에 부착 ===
    // 단일 객체를 붙임(부모 인덱스는 이 씬 기준). 유니크 네임/Tag/Layer/루트 children/Transform 부모까지 처리.
    GameObjectIndex AttachExistingGameObject(std::shared_ptr<GameObject> go, GameObjectIndex parentIndex);
    // DDOL 서브트리를 한꺼번에 붙임. parent/child 인덱스는 go들이 원래 갖고 있던 서브트리 상대관계를 따름.
    // 반환: oldIndex -> newIndex 매핑(이 씬 기준)
    std::unordered_map<GameObjectIndex, GameObjectIndex>
        AttachExistingGameObjectHierarchy(const std::vector<std::shared_ptr<GameObject>>& roots);
    std::shared_ptr<GameObject> GetGameObject(std::string_view name);
    const std::vector<GameObject*>& GetSelectedSceneObjects() const { return m_selectedSceneObjects; }
	void AddSelectedSceneObject(GameObject* sceneObject);
	void RemoveSelectedSceneObject(GameObject* sceneObject);
	void ClearSelectedSceneObjects();
	void AddRootGameObject(std::string_view name);
	void DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject);
	void DestroyGameObject(GameObjectIndex index);
	void CullMeshData();
	void InternalPauseUpdateForUI();

    std::vector<std::shared_ptr<GameObject>> CreateGameObjects(size_t createSize, GameObjectIndex parentIndex = -1);

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
	void CollectSpriteRenderer(SpriteRenderer* ptr);
	void UnCollectSpriteRenderer(SpriteRenderer* ptr);
	std::vector<SpriteRenderer*>& GetSpriteRenderers() { return m_spriteRenderers; }

public:
    void CollectTerrainComponent(TerrainComponent* ptr);
    void UnCollectTerrainComponent(TerrainComponent* ptr);
    std::vector<TerrainComponent*>& GetTerrainComponent() { return m_terrainComponents; }

public:
    void CollectFoliageComponent(FoliageComponent* ptr);
    void UnCollectFoliageComponent(FoliageComponent* ptr);
    std::vector<FoliageComponent*>& GetFoliageComponents() { return m_foliageComponents; }

public:
	void CollectDecalComponent(DecalComponent* ptr);
	void UnCollectDecalComponent(DecalComponent* ptr);
	std::vector<DecalComponent*>& GetDecalComponents() { return m_decalComponents; }

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

public:
	void AddCanvas(const std::shared_ptr<GameObject>& canvas);
	void RemoveCanvas(const std::shared_ptr<GameObject>& canvas);
	std::vector<std::weak_ptr<GameObject>>& GetCanvases() { return Canvases; }
	std::unordered_map<std::string, std::weak_ptr<GameObject>>& GetCanvasMap() { return CanvasMap; }
	std::shared_ptr<GameObject> FindCanvasName(std::string_view name);
	std::shared_ptr<GameObject> FindCanvasIndex(size_t index);

private:
    void DestroyGameObjects();
	void DestroyComponents();
    std::string GenerateUniqueGameObjectName(const std::string_view& name);
	void RemoveGameObjectName(const std::string_view& name);
    void UpdateModelRecursive(GameObjectIndex objIndex, Mathf::xMatrix model, bool recursive = false);

	// UI 레이아웃 순회의 유일한 구현. 부모의 rect·배율·변경 여부를 받아 자신을
	// 계산하고 자식으로 내려간다(PHASE 7-5).
	//
	// isTopLevel은 "부모가 UI 좌표계를 정해 주지 않는다"는 뜻이다. 이때 캔버스라면
	// 화면 크기로 직접 구동하고(7-1), 아니면 화면 rect를 부모로 삼는다(7-2).
	// visited는 같은 노드를 두 번 계산하지 않게 막는다 — 두 번째 방문은 배율을
	// 잘못된 값으로 덮어써서 캔버스 스케일러를 무력화한다.
	void LayoutUINode(GameObject* obj, const Mathf::Rect& parentRect,
		float parentScale, bool parentChanged, bool isTopLevel, int depth,
		std::unordered_set<GameObject*>& visited);

private:
	void SetInternalPhysicData();

public:
    void AllUpdateWorldMatrix();
	void AllUIUpdateWorldMatrix();

	// UI 레이아웃 전체를 한 번에 갱신한다(PHASE 7-5).
	//
	// 예전에는 같은 일이 세 군데에 흩어져 있었다 — UpdateModelRecursive의 UI 분기,
	// UpdateUIRecursive, 그리고 UpdateLayout 안의 자식 재귀. 어느 것이 언제 몇 번
	// 도는지 추론할 수 없었고, 앞의 둘이 병렬로 돌면서 세 번째의 재귀를 호출해
	// 교차 스레드로 같은 노드를 건드릴 수 있었다(분석 문서 F-9).
	//
	// 레이아웃은 부모→자식 의존 사슬이라 병렬화할 대상이 아니다. 여기서 직렬로
	// 한 번 돌고, 트랜스폼 행렬 갱신(비-UI)만 예전처럼 병렬로 남긴다.
	void UpdateUILayout();

	// 한 서브트리만 즉시 레이아웃한다. 프레임 패스를 기다릴 수 없는 곳
	// (에디터 드래그, UI 생성 직후)에서 쓴다. 순회 규칙은 프레임 패스와 공유한다.
	void LayoutUISubtree(GameObject* root);

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
	std::vector<DecalComponent*>	m_decalComponents;
	std::vector<SpriteRenderer*>	m_spriteRenderers;
	std::mutex sceneMutex{};

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

private:
	std::vector<std::weak_ptr<GameObject>>	Canvases;
	std::unordered_map<std::string, std::weak_ptr<GameObject>> CanvasMap;

public:
	HashingString GetSceneName() const { return m_sceneName; }
    std::vector<Texture*>		m_lightmapTextures{};
    std::vector<Texture*>		m_directionalmapTextures{};
    GameObject*					m_selectedSceneObject = nullptr;
	std::vector<GameObject*>	m_selectedSceneObjects;
    Core::DelegateHandle		resetObjHandle{};
public:
	std::vector<std::shared_ptr<MeshRenderer>> m_visibleMeshesScratch;
};
