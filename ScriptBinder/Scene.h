#pragma once
#include "LightProperty.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "EntityHandle.h"
#include "SystemSchedule.h"
#include "PhysicsManager.h"
#include "AssetBundle.h"
#include "Scene.generated.h"
#include "EBodyType.h"
// GameObject.h를 온전히 include한다 — ReflectScene의 meta_property(m_SceneObjects)가
// vector<shared_ptr<GameObject>> 리플렉션 등록에서 typeid(GameObject)를 요구하므로
// 전방 선언으로는 부족하다. 과거에는 이 include가
//   Scene.h → GameObject.h → GameObject.inl → Scene.h
// 순환을 닫아 금지였지만, 지금은 GameObject.inl이 Scene.h를 include하지 않는다
// (SceneObjectAt 우회 — GameObject.inl 상단 주석 참고). 이 자급자족은
// HeaderSelfSufficiency.cpp가 상시 검증한다.
#include "GameObject.h"
#include <unordered_map>

#pragma region forward_decl
// LifecycleRegistry.h를 여기서 포함하지 않는 이유:
//   Scene.h → LifecycleRegistry.h → Component.h → … 로 include 사슬이 길어질 뿐
// Scene.h가 필요한 것은 이름뿐이다. 전방 선언으로 끊고, 구현은 Scene.cpp에서 포함한다.
// (고정 기반 타입을 준 enum은 전방 선언이 가능하다)
namespace Lifecycle { enum PhaseBits : uint16_t; }
struct ICollider;
class Component;
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
    // 씬 루트 오브젝트 정본 조회(트랙 E3 후속 배선 — 통합 단계에서 배선).
    // GameObject::kSceneRootIndex(관례상 0)를 가리키던 리터럴 0 호출들
    // (CreateGameObject/LoadGameObject의 부모 폴백)이 이 접근자로 수렴한다.
    std::shared_ptr<GameObject> GetRootObject() { return GetGameObject(GameObject::kSceneRootIndex); }
    // EntityHandle 기반 조회(트랙 E1). 세대가 어긋나거나 슬롯이 비어 있으면
    // nullptr — TryGetGameObject(Index)와 달리 "그 인덱스가 지금 가리키는 것이
    // 핸들 발급 당시의 그 객체인가"까지 확인해, 슬롯 재사용 뒤의 낡은 핸들을 걸러낸다.
    GameObject* Resolve(EntityHandle handle) const;
    // index가 가리키는 슬롯의 현재 EntityHandle. 슬롯이 비어 있으면(범위 밖·
    // tombstone) 무효 핸들을 돌려준다.
    EntityHandle HandleOf(GameObjectIndex index) const;
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
	// 프록시 갱신 + UI 렌더 데이터. 예전 이름은 CullMeshData였는데,
	// 카메라별 컬링을 걷어낸 뒤로는(RenderSceneViewPlan ③) 컬링을
	// 하지 않는다 — 실제 컬링은 렌더 쪽 뷰가 절두체로 한다.
	void UpdateRenderData();
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

    // ── 슬롯맵 (SceneGraphRedesignPlan 트랙 E1) ──
    //
    // m_SceneObjects와 항상 같은 길이를 유지하는 세대 테이블. 슬롯을 해제할 때
    // 증가하고, 그 값 그대로 다음 입주자에게 물려준다 — 0은 절대 나오지
    // 않는다(EntityHandle의 "무효"와 겹치면 안 되므로 0을 건너뛴다).
    std::vector<uint32_t> m_generations;
    // tombstone(= nullptr)된 슬롯의 인덱스. 다음 할당이 여기서 먼저 꺼내 쓴다.
    std::vector<uint32_t> m_freeSlots;

    // 슬롯 할당 단일점. free 리스트가 있으면 재사용하고(세대는 해제 시 이미
    // 올라가 있다), 없으면 새로 늘린다. CreateGameObject/AddGameObject/
    // LoadGameObject/AttachExistingGameObject가 공유한다.
    GameObject::Index AllocateSlot();
    // 슬롯 해제 단일점. tombstone(reset)+세대 증가+free 리스트 등록을 한 곳에서
    // 한다 — DestroyGameObjects·DetachGameObjectHierarchy가 공유한다. 루트(0)는
    // 여기로 오면 안 된다(호출부가 먼저 걸러야 하지만 방어적으로 한 번 더 막는다).
    void ReleaseSlot(GameObject::Index index);
    // index를 부모(또는 부모가 없으면 씬 루트)의 children 목록에서 뗀다.
    void UnlinkFromParentChildren(GameObject::Index index);

public:
    // 생명주기 델리게이트 15종이 여기 있었다. PHASE 9-3에서 철거했다.
    //
    // 여덟 종(Awake·OnEnable·Start·FixedUpdate·Update·LateUpdate·OnDisable·OnDestroy)은
    // 9-1의 단계 리스트가, 활성 전이는 Component::SetEnabled가 대신한다.
    //
    // 물리 여섯 종(OnTrigger*·OnCollision*)과 InternalPhysicsUpdateEvent는 철거 시점에
    // 이미 죽어 있었다 — 선언되고 소멸자에서 Clear될 뿐 브로드캐스트도 구독도 없었다.
    // 실제 물리 콜백은 Scene::OnTriggerEnter 등이 ClrHost 큐로 보낸다(2-20).
    //
    // 남은 Delegate는 시스템 이벤트용이다(sceneLoadedEvent·OnResizeEvent 등).
    // 구독자가 진짜로 동적인 곳에서는 여전히 옳은 도구다 — 뺀 것은 프레임 루프뿐이다.

public:
    // ── 생명주기 레지스트리 (PHASE 9-1) ──
    //
    // 생명주기 디스패치의 유일한 경로다(PHASE 9-3에서 델리게이트를 철거했다).
    //
    // 원소가 raw Component*인 것은 소유가 GameObject의 shared_ptr에 있기 때문이다.
    // 그 포인터가 뜰 수 없는 이유는 파괴가 프레임 끝 한 지점에서만 일어나고, 그때
    // 리스트에서 먼저 빼기 때문이다 — 순회 중에 리스트가 바뀌는 상황 자체가 없다.
    // (델리게이트 경로는 순회 중 파괴가 가능했고, 그것이 R1·R2였다)

    /// 컴포넌트를 레지스트리에 편입한다. 마스크를 보고 해당 단계 리스트에만 넣는다.
    /// 등록되지 않은 타입이면 오류로 남기고 편입하지 않는다(조용히 넘어가지 않는다).
    /// 내부적으로 SystemSchedule::SubscribeImplicit을 부른다(트랙 C1·L4) — 판정은
    /// 여전히 여기(Lifecycle::Registry 마스크)가 하고, 저장은 SystemSchedule이 한다.
    void RegisterComponent(Component* component);
    /// 리스트에서 뺀다. swap-and-pop이라 O(1)이고 순서는 보존하지 않는다 —
    /// 순서를 보존해야 하는 것은 단계 사이지 같은 단계 안이 아니다.
    /// 내부적으로 SystemSchedule::UnsubscribeAll을 부른다.
    void UnregisterComponent(Component* component);

    /// 명시 구독 API 진입점 (트랙 L4). 새 컴포넌트가 훅 오버라이드 없이
    /// `scene->Schedule().Subscribe(this, SystemSchedule::Phase::Update)` 식으로
    /// 틱을 구독할 수 있다 — 소비자 배선(가상 Update의 시스템 이관, 트랙 C3)은
    /// 이 슬라이스의 범위 밖이다. 여기서는 경로만 연다.
    SystemSchedule& Schedule() { return m_schedule; }

    /// 순회 한복판에서 파괴·생성을 일으켜 재진입 안전을 강제로 시험한다 (PHASE 9-9).
    ///
    /// 9-0에서 이 재현을 미뤄 뒀다 — 당시 구조에는 "순회 중"이라는 지점을 안전하게
    /// 잡을 자리가 없었다. 레지스트리가 선 지금은 RegistryTick의 루프 한가운데가
    /// 정확히 그 자리다.
    ///
    /// 여기까지 R1(순회 중 UAF)·R2(즉시 파괴)가 닫혔다는 근거는 설계 논증과 회귀
    /// 통과뿐이었다. 그 둘은 "그런 일이 일어나지 않았다"이지 "일어나도 안전하다"가
    /// 아니다. ASan 아래에서 일부러 일으켜 봐야 후자를 말할 수 있다.
    enum class StressKind : int { Destroy, AddComponent, Both };
    void ArmReentrancyStress(StressKind kind, int count);
private:
    void FireReentrancyStress(bool midTraversal);
public:

    /// 프레임 끝의 유일한 파괴 지점. 파괴 표시된 것들의 OnDisable→OnDestroy를 부르고
    /// 리스트에서 뺀다. 실제 메모리 해제는 기존 DestroyGameObjects가 이어서 한다.
    void FlushPendingDestroy();

    /// 진단용 — 각 리스트 크기.
    struct RegistryCounts { size_t pendingAwake, pendingStart, update, lateUpdate, fixedUpdate; };
    RegistryCounts GetRegistryCounts() const;

    /// 진단용 — 암묵/명시 구독 잔존 수(트랙 L4 래칫 측정 기반). 프로파일러
    /// 연동은 범위 밖 — 카운터만 노출한다.
    SystemSchedule::SubscriptionCounts GetSubscriptionCounts() const { return m_schedule.GetSubscriptionCounts(); }

private:
    // ── 페이즈 리스트 저장소 (트랙 C1·L4 — SceneGraphRedesignPlan §4) ──
    //
    // 예전에는 벡터 6종(m_pendingAwake·m_pendingStart·m_updateList·
    // m_lateUpdateList·m_fixedUpdateList·m_destroyWatchList)이 여기 직접
    // 흩어져 있었다. 지금은 SystemSchedule 하나가 들고 Scene은 위임한다 —
    // RegisterComponent/UnregisterComponent/RegistryDrainAwakeAndStart/
    // RegistryTick/FlushPendingDestroy의 호출 순서·대상 집합은 이 편입으로
    // 바뀌지 않는다(회귀 세트 생명주기 순서 92 사건 불변 게이트).
    SystemSchedule m_schedule;

    // 레지스트리 경로의 단계 실행. 위 Awake()/Update() 등이 스위치를 보고 부른다.
    void RegistryDrainAwakeAndStart();
    void RegistryTick(std::vector<Component*>& list, Lifecycle::PhaseBits phase, float delta);

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
	// 아래 넷은 편집기 부기다. 그리는 값은 LightRenderProxy가 든다 —
	// UpdateLight(렌더러로 가던 매 프레임 복사)는 그래서 사라졌다.
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
    void UpdateModelRecursive(GameObjectIndex objIndex, Mathf::xMatrix model, bool recursive = false,
        std::unordered_set<GameObjectIndex>* visited = nullptr, int depth = 0);

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
