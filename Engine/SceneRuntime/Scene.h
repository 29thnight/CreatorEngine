#pragma once
#include "LightProperty.h"
#include "GameObjectType.h"
#include "GameObjectIndex.h"
#include "EntityHandle.h"
#include "AuthoringNodeView.h" // D3-a-5
#include "SystemSchedule.h"
#include "CameraSystem.h"
#include "PhysicsManager.h"
#include "AssetBundle.h"
#include "TransformStore.h"
#include "HierarchyStore.h"
#include "DetachedEntityTransfer.h"
#include "RenderProxyDirty.h"
#include "EBodyType.h"
#include <mathematics/rect.hpp>
// Entity.h를 온전히 include한다 — ReflectScene의 meta_property(m_Entities)가
// vector<unique_ptr<Entity>> 리플렉션 등록에서 typeid(GameObject)를 요구하므로
// 전방 선언으로는 부족하다. 과거에는 이 include가
//   Scene.h → Entity.h → Entity.inl → Scene.h
// 순환을 닫아 금지였지만, 지금은 Entity.inl이 Scene.h를 include하지 않는다
// (EntityAt 우회 — Entity.inl 상단 주석 참고). 이 자급자족은
// HeaderSelfSufficiency.cpp가 상시 검증한다.
#include "Entity.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#pragma region forward_decl
// LifecycleRegistry.h를 여기서 포함하지 않는 이유:
//   Scene.h → LifecycleRegistry.h → Component.h → … 로 include 사슬이 길어질 뿐
// Scene.h가 필요한 것은 이름뿐이다. 전방 선언으로 끊고, 구현은 Scene.cpp에서 포함한다.
// (고정 기반 타입을 준 enum은 전방 선언이 가능하다)
namespace Lifecycle { enum PhaseBits : uint16_t; }
class Component;
class RenderScene;
class SceneManager;
class Scene;
template<typename Context> class BasicTweenManager;
using TweenManager = BasicTweenManager<Scene>;
class LightComponent;
class MeshRenderer;
class RigidBodyComponent;
class TerrainComponent;
class FoliageComponent;
class DecalComponent;
class SpriteRenderer;
class ImageComponent;
class TextComponent;
class SpriteSheetComponent;
class ReferenceAssets;
class BoxColliderComponent;
class SphereColliderComponent;
class CapsuleColliderComponent;
class MeshColliderComponent;
class CharacterControllerComponent;
class TerrainColliderComponent;
class Animator;
struct TransformExecutionGraphState;
struct SceneRenderRegistryState;
#pragma endregion forward_decl

enum class TransformSyncPoint : uint8_t
{
	Unspecified,
	FixedUpdate,
	PreUpdate,
	LateUpdate,
	SceneLoad,
	Benchmark,
	Count
};

struct TransformTopologyMutationCounters
{
	// Scene graph membership changes, not GameObject allocation lifetime. A
	// cross-scene/DDOL transfer is therefore one removal plus one insertion.
	uint64_t created = 0;
	uint64_t destroyed = 0;
	uint64_t reparented = 0;
};

enum class ReparentResult : uint8_t
{
	Success,
	NoChange,
	InvalidHandle,
	StaleHandle,
	CrossScene,
	RootRejected,
	SelfRejected,
	CycleRejected,
	CorruptHierarchy
};

const char* ReparentResultName(ReparentResult result);

struct HierarchyIntegrityMetrics
{
	uint64_t parentChildMismatch = 0;
	uint64_t orphan = 0;
	uint64_t duplicateChild = 0;
	uint64_t invalidReference = 0;

	uint64_t Total() const
	{
		return parentChildMismatch + orphan + duplicateChild + invalidReference;
	}
};

// TransformUpdatePlan X4의 compiled projection 진단 표면. ExecIndex와 packed
// 배열 자체는 Scene.cpp의 비공개 상태에만 존재한다. 외부에는 stable EntityHandle과
// 집계만 노출해 실행 위치가 저작 identity/직렬화 포맷으로 새지 않게 한다.
struct ExecutionGraphCompileMetrics
{
	bool success = false;
	uint64_t topologyVersion = 0;
	uint64_t compiledVersion = 0;
	uint64_t compileCount = 0;
	double compileUs = 0.0;
	uint64_t entitySlots = 0;
	uint64_t occupiedEntities = 0;
	uint64_t spatialNodes = 0;
	uint64_t layoutNodes = 0;
	uint64_t transformlessSpatial = 0;
	uint64_t nonLayoutMember = 0;
	uint64_t mappingViolations = 0;
	uint64_t parentOrderViolations = 0;
	uint64_t subtreeRangeViolations = 0;
	uint64_t hierarchyViolations = 0;
	uint64_t unreachableEntities = 0;
	uint64_t cycleViolations = 0;

	uint64_t TotalViolations() const
	{
		return transformlessSpatial + nonLayoutMember + mappingViolations
			+ parentOrderViolations + subtreeRangeViolations
			+ hierarchyViolations + unreachableEntities + cycleViolations;
	}
};

struct ExecutionGraphRelationDiagnostics
{
	bool spatialMember = false;
	bool layoutMember = false;
	EntityHandle spatialParent{};
	EntityHandle layoutParent{};
	uint32_t spatialSubtreeSize = 0;
	uint32_t layoutSubtreeSize = 0;
};

struct SpatialResolveMetrics
{
	bool resolved = false;
	bool sparseRequested = false;
	bool sparseExecuted = false;
	bool legacyFallback = false;
	bool fullResolve = false;
	double resolveUs = 0.0;
	uint64_t dirtyRequests = 0;
	uint64_t staleRequests = 0;
	uint64_t canonicalRanges = 0;
	uint64_t mergedRequests = 0;
	uint64_t resolvedNodes = 0;
	uint64_t localComposes = 0;
	uint64_t worldWrites = 0;
};

struct SpatialPullMetrics
{
	bool attempted = false;
	bool resolved = false;
	bool packed = false;
	bool legacyFallback = false;
	bool staleHandle = false;
	bool queuePreserved = false;
	bool propagationSignalPreserved = false;
	uint64_t pathNodes = 0;
	uint64_t recomputedNodes = 0;
	uint64_t localComposes = 0;
	uint64_t worldWrites = 0;
	uint64_t pendingRequestsBefore = 0;
	uint64_t pendingRequestsAfter = 0;
	uint64_t dirtyEpochBefore = 0;
	uint64_t dirtyEpochAfter = 0;
};

// TransformUpdatePlan X7: worker pose/physics 결과를 frame barrier 뒤 packed
// transform storage에 합치는 계측이다. bindLookups는 skeleton serial 또는
// topology가 바뀐 binding pass에서만 증가해야 하며 steady pose upload에서는 0이다.
struct AnimatorPoseUploadMetrics
{
	bool attempted = false;
	bool uploaded = false;
	bool packed = false;
	bool legacyFallback = false;
	bool rebound = false;
	bool disabled = false;
	bool staleOwner = false;
	bool skeletonMissing = false;
	uint64_t skeletonSerial = 0;
	uint64_t bindLookups = 0;
	uint64_t validBones = 0;
	uint64_t invalidBones = 0;
	uint64_t localWrites = 0;
	uint64_t queuedRoots = 0;
	// 팔레트가 바뀐 프레임에 스킨 프록시를 몇 개 dirty로 올렸는가.
	// 0인데 localWrites > 0이면 최신 팔레트가 렌더로 못 간다.
	uint64_t paletteDirty = 0;
};

struct TransformWorldWrite
{
	EntityHandle target{};
	math::matrix4x4 world{ math::matrix4x4::identity() };
};

struct TransformBulkWriteMetrics
{
	TransformWriteReason reason = TransformWriteReason::CppSetter;
	bool packed = false;
	bool legacyFallback = false;
	uint64_t requested = 0;
	uint64_t accepted = 0;
	uint64_t stale = 0;
	uint64_t localWrites = 0;
	uint64_t worldWrites = 0;
	uint64_t queuedRoots = 0;
	uint64_t epochAdvances = 0;
};

// TransformUpdatePlan X0 진단 스냅샷. ui/spatial/dispatch는 한 번의 sync가 실제로
// 걸린 wall time이고, visit/compose/multiply/decompose는 병렬 worker에서 합산한
// CPU work다. 따라서 worker 합계를 wall time에 다시 더하면 안 된다.
struct TransformUpdateMetrics
{
	TransformSyncPoint syncPoint = TransformSyncPoint::Unspecified;
	bool uiDomainResolved = false;
	bool spatialDomainResolved = false;
	double totalUs = 0.0;
	double uiUs = 0.0;
	double spatialUs = 0.0;
	double dispatchUs = 0.0;
	double visitWorkerUs = 0.0;
	double localComposeWorkerUs = 0.0;
	double worldMultiplyWorkerUs = 0.0;
	double decomposeWorkerUs = 0.0;

	uint64_t spatialVisitCount = 0;
	uint64_t localComposeCount = 0;
	uint64_t worldMultiplyCount = 0;
	uint64_t decomposeCount = 0;
	uint64_t rootDispatchCount = 0;

	uint64_t entityCount = 0;
	uint64_t transformOnlyCount = 0;
	uint64_t rectOnlyCount = 0;
	uint64_t transformAndRectCount = 0;
	uint64_t neitherCount = 0;
	uint64_t transformDirtyCount = 0;
	uint64_t rectDirtyCount = 0;
};

struct TransformWriteMetrics
{
	uint64_t publishEpoch = 0;
	uint64_t windowStartEpoch = 0;
	uint64_t total = 0;
	uint64_t invalidHandle = 0;
	std::array<uint64_t, kTransformWriteReasonCount> byReason{};
};

class Scene
{
   public:
   static consteval auto reflect()
   {
       using Self = Scene;
       return meta::schema<Self>(
           meta::field<&Self::m_Entities>,
           meta::field<&Self::m_buildIndex>,
           meta::field<&Self::m_sceneName>,
           meta::field<&Self::m_requiredLoadAssetsBundle>);
   }
public:
	class HierarchyBulkBuildScope
	{
	public:
		~HierarchyBulkBuildScope();
		void Complete() noexcept;
		HierarchyBulkBuildScope(const HierarchyBulkBuildScope&) = delete;
		HierarchyBulkBuildScope& operator=(const HierarchyBulkBuildScope&) = delete;
		HierarchyBulkBuildScope(HierarchyBulkBuildScope&& other) noexcept;
		HierarchyBulkBuildScope& operator=(HierarchyBulkBuildScope&&) = delete;

	private:
		friend class Scene;
		explicit HierarchyBulkBuildScope(Scene& scene);
		Scene* m_scene = nullptr;
	};

	Scene();
	~Scene();

	// Entity의 단독 소유자. 외부에는 프레임 경계를 넘지 않는 raw pointer 또는
	// EntityHandle만 노출한다. DDOL 이송은 unique_ptr 자체를 Scene 간 이동한다.
	std::vector<std::unique_ptr<Entity>> m_Entities;
	std::future<void> m_AIFuture;

	Entity* AddEntity(std::unique_ptr<Entity> entity);
	Entity* CreateEntity(std::string_view name, GameObjectType type = GameObjectType::Empty, Entity::Index parentIndex = -1);
	Entity* LoadEntity(size_t instanceID, std::string_view name, GameObjectType type = GameObjectType::Empty, Entity::Index parentIndex = -1);
	Entity* GetEntity(Entity::Index index);
    Entity* TryGetEntity(Entity::Index index);
    // 씬 루트 오브젝트 정본 조회(트랙 E3 후속 배선 — 통합 단계에서 배선).
    // Entity::kSceneRootIndex(관례상 0)를 가리키던 리터럴 0 호출들
    // (CreateEntity/LoadEntity의 부모 폴백)이 이 접근자로 수렴한다.
    Entity* GetRootEntity() { return GetEntity(Entity::kSceneRootIndex); }
    // EntityHandle 기반 조회(트랙 E1). 세대가 어긋나거나 슬롯이 비어 있으면
    // nullptr — TryGetEntity(Index)와 달리 "그 인덱스가 지금 가리키는 것이
    // 핸들 발급 당시의 그 객체인가"까지 확인해, 슬롯 재사용 뒤의 낡은 핸들을 걸러낸다.
    Entity* Resolve(EntityHandle handle) const;
    // index가 가리키는 슬롯의 현재 EntityHandle. 슬롯이 비어 있으면(범위 밖·
    // tombstone) 무효 핸들을 돌려준다.
    EntityHandle HandleOf(Entity::Index index) const;
	ReparentResult Reparent(EntityHandle child, EntityHandle newParent);
	uint64_t GetTopologyVersion() const
	{
		return m_topologyVersion.load(std::memory_order_acquire);
	}
	HierarchyIntegrityMetrics GetHierarchyIntegrityMetrics() const;
	const ExecutionGraphCompileMetrics& GetExecutionGraphCompileMetrics() const;
	ExecutionGraphRelationDiagnostics GetExecutionGraphRelationDiagnostics(
		EntityHandle entity) const;
	// Engine component ownership paths call this when Transform/Rect/Canvas membership
	// changes without a hierarchy edit. It only publishes derived topology dirtiness.
	void RecordExecutionGraphMembershipChanged();
	const SpatialResolveMetrics& GetLastSpatialResolveMetrics() const;
	const SpatialPullMetrics& GetLastSpatialPullMetrics() const;
	HierarchyBulkBuildScope BeginHierarchyBulkBuild();
    // 슬롯 점유자만 확인하는 raw 접근자
    // (SceneGraphRedesignPlan §4 트랙 S, S1). Transform::ResolveStore가 매
    // 접근마다 "이 슬롯의 진짜 점유자가 나 자신인가"를 확인하는 핫패스라
    // TryGetEntity(shared_ptr 반환)보다 이쪽을 쓴다.
    Entity* GetEntityRaw(Entity::Index index) const
    {
        if (index < 0 || static_cast<size_t>(index) >= m_Entities.size()) return nullptr;
        return m_Entities[index].get();
    }
    // 계층·트랜스폼 파생 데이터(로컬/월드 행렬·dirty·월드 캐시)의 유일한 정본
    // (SceneGraphRedesignPlan §4 트랙 S, S1). 슬롯 인덱스는 m_Entities와
    // 평행이다 — AllocateSlot/ReleaseSlot이 동기해서 늘리고 리셋한다.
    TransformStore& GetTransformStore() { return m_transformStore; }
    const TransformStore& GetTransformStore() const { return m_transformStore; }
    const HierarchyStore& GetHierarchyStore() const { return m_hierarchyStore; }
    // H3 이후 값 복사본과의 대조가 아니라 Entity 슬롯 존재 여부와 Store occupied가
    // 평행한지만 센다. 기존 회귀 출력 이름은 호환을 위해 유지한다.
    size_t CountHierarchyStoreMismatches() const;
    // Detach an Entity subtree from this scene for DontDestroyOnLoad rebind
    void DetachEntityHierarchy(Entity* root, std::vector<DetachedEntityTransfer>& detached);
    // === C안: 공식 경로로 기존 객체(DDOL)를 이 씬에 부착 ===
    // 단일 객체를 붙임(부모 인덱스는 이 씬 기준). 유니크 네임/Tag/Layer/루트 children/Transform 부모까지 처리.
    Entity::Index AttachExistingEntity(std::unique_ptr<Entity> entity, Entity::Index parentIndex);
    // DDOL 서브트리를 한꺼번에 붙임. parent/child 인덱스는 go들이 원래 갖고 있던 서브트리 상대관계를 따름.
    // 반환: oldIndex -> newIndex 매핑(이 씬 기준)
    std::unordered_map<Entity::Index, Entity::Index>
        AttachExistingEntityHierarchy(std::vector<DetachedEntityTransfer>& entities);
    Entity* GetEntity(std::string_view name);
	void AddSelectedEntity(Entity* entity);
	void RemoveSelectedEntity(Entity* entity);
	void ClearSelectedEntities();
	void AddRootEntity(std::string_view name);
	void DestroyEntity(Entity* entity);
	void DestroyEntity(Entity::Index index);
	// 프록시 갱신 + UI 렌더 데이터. 예전 이름은 CullMeshData였는데,
	// 카메라별 컬링을 걷어낸 뒤로는(RenderSceneViewPlan ③) 컬링을
	// 하지 않는다 — 실제 컬링은 렌더 쪽 뷰가 절두체로 한다.
	void UpdateRenderData();

	// 렌더 프록시 커밋 단계 (트랙 S · S4). UpdateRenderData 안에 인라인으로 있던
	// 것을 뽑았다 — 벤치(scene.proxybench)가 이 단계만 따로 재고, S4의 변경분
	// 커밋도 여기 하나에서 이뤄진다.
	void CommitRenderProxies();
	// 커밋 대상 컴포넌트 총수 — 벤치가 "무엇을 얼마나 쟀는지" 함께 보고한다.
	size_t RenderProxyComponentCount() const;
	bool PublishRenderProxyDirty(Component* component, ProxyDirty dirty);
	size_t PublishRenderProxyDirty(EntityHandle owner, ProxyDirty dirty);
	RenderProxyCommitMetrics GetRenderProxyCommitMetrics() const;
	void ResetRenderProxyCommitMetrics();
	void InternalPauseUpdateForUI();

    // CreateEntities(일괄 생성)·InsertEntities(직삽입)는 호출자 0으로 확인돼
    // 걷어냈다(2026-08-20 전수 추적). 특히 InsertEntities는 AllocateSlot을
	// 우회해 m_generations·m_transformStore와의 평행을 깨는 경로였다 — 살아
	// 있었다면 결함이고, 죽어 있었으니 제거가 정답이다.

private:
    friend class Entity;
    friend class SceneManager;
    //for Editor
    void Reset();

    // 이름 충돌 방지
    std::string MakeUniqueName(std::string_view base);

    // ── 슬롯맵 (SceneGraphRedesignPlan 트랙 E1) ──
    //
    // m_Entities와 항상 같은 길이를 유지하는 세대 테이블. 슬롯을 해제할 때
    // 증가하고, 그 값 그대로 다음 입주자에게 물려준다 — 0은 절대 나오지
    // 않는다(EntityHandle의 "무효"와 겹치면 안 되므로 0을 건너뛴다).
    std::vector<uint32_t> m_generations;
    // tombstone(= nullptr)된 슬롯의 인덱스. 다음 할당이 여기서 먼저 꺼내 쓴다.
    std::vector<uint32_t> m_freeSlots;

    // ── 씬 식별자 (트랙 W — EntityHandle에 씬 스코프 도입) ──
    //
    // EntityHandle::sceneId에 실려 "이 슬롯이 어느 씬 것인가"를 구분하는 값.
    // 생성자에서 딱 한 번 NextSceneId()로 받고 이후 절대 바뀌지 않는다 — Scene은
    // std::future(m_AIFuture) 멤버 때문에 복사가 불가능하고 사용자 선언 소멸자가
    // 암묵 이동도 막는 타입이라, 한 번 배정된 값이 다른 인스턴스와 섞일 길이 없다.
    // (한때 std::mutex 멤버도 이 논거였으나 잠금으로 쓰인 적이 없어 걷어냈다.)
    //
    // reflect()에 올리지 않는다 — 프로세스 실행마다 새로 매기는 런타임 전용
    // 값이라 저장했다 복원해 봐야 의미가 없다(RenderEngine/Skeleton.h의
    // m_serial과 같은 이유로 직렬화 대상이 아니다).
    const uint32_t m_sceneId;
    // 씬 생성마다 단조 증가하는 일련번호 발급. SceneManager::m_scenes에서의
    // 위치(vector index)는 쓰지 않는다 — 씬이 삭제되면 그 위치가 다음 씬에게
    // 재사용돼 ABA가 난다(Skeleton::NextSerial 선례와 같은 사유, 그쪽 주석 참고).
    // 0은 "무효/미지정"으로 비워 두려고 1부터 발급한다(Scene.cpp 구현).
    static uint32_t NextSceneId();

    // 트랜스폼 파생 데이터 SoA 스토어(SceneGraphRedesignPlan §4 트랙 S, S1).
    // m_Entities·m_generations와 평행 — AllocateSlot/ReleaseSlot이 동기한다.
    TransformStore m_transformStore;
    // SceneGraph 계층의 유일 정본. Entity는 슬롯/정체성과 컴포넌트만 보유하고,
    // 런타임 읽기·쓰기와 YAML 어댑터가 모두 이 Store를 사용한다(H3).
    HierarchyStore m_hierarchyStore;
	// X4 derived projection. PIMPL로 ExecIndex와 packed 배열을 이 헤더 밖에 가둔다.
	// reflect()에 없으므로 Entity slot/generation 및 디스크 identity와 독립이다.
	std::unique_ptr<TransformExecutionGraphState> m_executionGraphs;
	// 렌더 컴포넌트 membership, light index 부기, 재사용 snapshot을 한 도메인으로
	// 묶는다. Scene은 Entity와 phase orchestration만 드러내고 구체 컨테이너는
	// Scene.cpp에 가둔다. 직렬화 대상이 아니다(reflect()에 넣지 않는다).
	std::unique_ptr<SceneRenderRegistryState> m_renderRegistry;

    // 슬롯 할당 단일점. free 리스트가 있으면 재사용하고(세대는 해제 시 이미
    // 올라가 있다), 없으면 새로 늘린다. CreateEntity/AddEntity/
    // LoadEntity/AttachExistingEntity가 공유한다.
    Entity::Index AllocateSlot();
    // 슬롯 해제 단일점. tombstone(reset)+세대 증가+free 리스트 등록을 한 곳에서
    // 한다 — DestroyEntities·DetachEntityHierarchy가 공유한다. 루트(0)는
    // 여기로 오면 안 된다(호출부가 먼저 걸러야 하지만 방어적으로 한 번 더 막는다).
    std::unique_ptr<Entity> ReleaseSlot(Entity::Index index);
	// H3 저장 어댑터: Entity node에 Store 정본을 기존 계층 키로 쓴다.
	// Entity::OnAfterSerialize만 호출하며 detached/비점유 Entity에는 쓰지 않는다.
	void SerializeEntityHierarchy(const Entity& entity, const Authoring::MutableNodeView& node) const;
	// 비소유 AI registry/component snapshot이 Entity를 읽는 동안 파괴·이송하지
	// 않도록 Scene의 구조 변경 경계에서 future를 회수한다.
	void DrainAIUpdate();
    // index를 부모(또는 부모가 없으면 씬 루트)의 children 목록에서 뗀다.
    void UnlinkFromParentChildren(Entity::Index index);

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
    /// `scene->Schedule().Subscribe(this, SystemSchedule::Phase::DestroyWatch)`
    /// 식으로 페이즈를 구독할 수 있다. 여기서 예시로 들었던
    /// `SystemSchedule::Phase::Update`는 트랙 C3 완결로 철거됐다 —
    /// 네이티브 컴포넌트의 프레임 틱은 이제 이 API를 거치지 않고 전용 시스템
    /// (AnimatorSystem 등)의 조밀 vector가 돈다(SystemSchedule.h 클래스 주석 참고).
    SystemSchedule& Schedule() { return m_schedule; }
	CameraSystem& Cameras() noexcept { return m_cameraSystem; }
	const CameraSystem& Cameras() const noexcept { return m_cameraSystem; }
	TweenManager& Tweens() noexcept;
	const TweenManager& Tweens() const noexcept;

    /// 순회 한복판에서 파괴·생성을 일으켜 재진입 안전을 강제로 시험한다 (PHASE 9-9).
    ///
    /// 9-0에서 이 재현을 미뤄 뒀다 — 당시 구조에는 "순회 중"이라는 지점을 안전하게
    /// 잡을 자리가 없었다. 레지스트리가 선 지금은 RegistryTick의 루프 한가운데가
    /// 정확히 그 자리다.
    ///
    /// 여기까지 R1(순회 중 UAF)·R2(즉시 파괴)가 닫혔다는 근거는 설계 논증과 회귀
    /// 통과뿐이었다. 그 둘은 "그런 일이 일어나지 않았다"이지 "일어나도 안전하다"가
    /// 아니다. ASan 아래에서 일부러 일으켜 봐야 후자를 말할 수 있다.
    ///
    /// ── 트랙 C·C2-0: 발화점이 한 번 죽었다가 시스템 루프로 되돌아온 사연 ──
    ///
    /// RegistryTick이 살아 있던 시절엔 이 루프 한가운데가 바로 그 함수 안이었다.
    /// 트랙 C3가 Component의 가상 Update/LateUpdate/FixedUpdate를 걷어내고 네이티브
    /// 틱을 전용 시스템(AnimatorSystem 등)의 조밀 vector로 옮기면서 RegistryTick
    /// 자체가 소멸했다(3d8ff9a4) — 그와 함께 "순회 중"이라는 발화 자리도 같이
    /// 사라졌다. 그 뒤로 PumpReentrancyStress()만 남아 매 페이즈 진입부(루프 밖)에서
    /// 터뜨렸는데, 그건 R1·R2가 이미 설계상 닫혀 있다는 것만 재확인할 뿐 "순회
    /// 도중"이라는 조건 자체를 시험하지 못했다 — 이빨 빠진 시험이었다.
    ///
    /// 이 시험이 실제로 지키는 것: Destroy는 프레임 끝 FlushPendingDestroy 한
    /// 지점에서만 물리적으로 벗겨지고(GameObject::Destroy는 마크만), 새 컴포넌트의
    /// 등록은 PendingAwake 큐를 거쳐 다음 프레임에야 각 시스템 vector에 들어간다
    /// (Scene::RegisterComponent). 이 "3겹 지연"이 유지되는 한 시스템 루프
    /// 한복판에서 파괴·생성을 일으켜도 그 루프가 도는 vector 자체는 흔들리지
    /// 않는다 — 그 전제가 앞으로도 유지되는지를 지키는 것이 이 시험의 값어치다
    /// (RemoveComponent가 즉시 삭제로 바뀌거나, 어느 시스템이 자기 루프 안에서
    /// 컴포넌트를 동기적으로 등록/해지하도록 바뀌는 순간 이 시험이 잡아야 한다).
    ///
    /// 되돌린 자리는 CameraSystem::Update다(감사 근거: 회귀 씬 4종 전부에서
    /// GetCount()==1로 비어 있지 않고, 루프 본문이 렌더 커맨드 등 외부 부작용
    /// 없는 순수 필드 대입이라 재진입 신호가 다른 부작용과 섞이지 않는다).
    /// CameraSystem은 Scene 소유지만 재진입 시험 상태를 알지 않는다. 그래서
    /// Scene::Update가 CameraSystem::Update에 std::function<void()> 하나를
    /// 넘기고, 그 함수 안에서만
    /// TryFireReentrancyStressMidTraversal을 부른다.
    ///
    /// 순서 규약과 그것이 FixedUpdate의 옛 폴백 호출을 없앤 이유: 무장은 반드시
    /// "순회 중 발화 → (못 잡으면) 같은 프레임 안의 폴백" 순으로 소비돼야 한다.
    /// 그런데 PlayerMain::Update는 매 프레임 SceneManagers->Physics(FixedUpdate가
    /// 여기서 돈다)를 SceneManagers->GameLogic(Update·LateUpdate가 돈다) 앞에
    /// 무조건 부른다 — 고정 타임스텝 누산기로 걸러지는 게 아니라 프레임마다
    /// 정확히 한 번이다. FixedUpdate에 폴백을 남겨 뒀다면 Update의 CameraSystem
    /// 루프가 한 번도 돌기 전에 그 폴백이 무장을 항상 먼저 가로채 이 시험 전체가
    /// 죽은 코드가 됐을 것이다 — 그래서 FixedUpdate의 폴백 호출을 뺐다
    /// (Scene.cpp의 FixedUpdate 상단 주석 참고). Update·LateUpdate는 항상 같은
    /// GameLogic() 호출 안에서 Update가 먼저이므로 이 문제가 없다.
    enum class StressKind : int { Destroy, AddComponent, Both };
    void ArmReentrancyStress(StressKind kind, int count);

    /// 시스템 루프 한복판에서 부르는 발화 지점(위 트랙 C·C2-0 참고).
    /// 무장돼 있지 않으면 bool 하나 읽고 즉시 반환한다 — 매 프레임 시스템
    /// 루프 안에서 불리는 핫패스이므로 그 이상 비용을 지우면 안 된다.
    /// systemName·loopLabel은 로그에만 쓰인다(예: "CameraSystem", "Update") —
    /// 회귀 스크립트가 "어느 시스템의 어느 루프에서 터졌는지"를 문자열로
    /// 가를 수 있어야 한다는 요구를 여기서 만족시킨다.
    void TryFireReentrancyStressMidTraversal(const char* systemName, const char* loopLabel);
private:
    /// origin은 로그 전용 식별자다 — 순회 중 발화면 "시스템::루프", 폴백이면
    /// 어느 페이즈(Update/LateUpdate — FixedUpdate는 폴백 호출부가 없다,
    /// PumpReentrancyStress 선언부 참고)에서 터졌는지를 담는다.
    void FireReentrancyStress(bool midTraversal, const std::string& origin);
public:

    /// 프레임 끝의 유일한 파괴 지점. 파괴 표시된 것들의 OnDisable→OnDestroy를 부르고
    /// 리스트에서 뺀다. 실제 메모리 해제는 기존 DestroyEntities가 이어서 한다.
    void FlushPendingDestroy();

    /// 진단용 — 각 리스트 크기. update·lateUpdate·fixedUpdate 세 필드는 트랙 C3
    /// 완결로 SystemSchedule에서 그 리스트 자체가 철거되며 함께 뺐다 —
    /// 구독자가 0인 리스트의 크기를 진단으로 남겨 봐야 항상 0만 찍혔다
    /// (SystemSchedule.h 클래스 주석 참고). destroyWatch는 원래도 이 구조체에
    /// 없었다 — 그 관례를 그대로 따른다.
    struct RegistryCounts { size_t pendingAwake, pendingStart; };
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
    // FlushPendingDestroy의 호출 순서·대상 집합은 이 편입으로 바뀌지 않는다
    // (회귀 세트 생명주기 순서 92 사건 불변 게이트).
    //
    // 그 여섯 중 셋(m_updateList·m_lateUpdateList·m_fixedUpdateList)은 트랙 C3가
    // Component의 가상 Update/LateUpdate/FixedUpdate를 걷어내고 네이티브
    // 컴포넌트의 틱을 전용 시스템(AnimatorSystem 등)의 조밀 vector로 옮기면서
    // 구독자가 0이 됐다 — 옛 RegistryTick도 그 시점에 소멸했다(3d8ff9a4). 그
    // 셋을 SystemSchedule에서 철거했으니(SystemSchedule.h) 지금 여기 남는 것은
    // PendingAwake·PendingStart·DestroyWatch 셋뿐이다.
    SystemSchedule m_schedule;
	CameraSystem m_cameraSystem;
	// math::tween<T> 값과 engine binding의 Scene-scoped 소유자. 전방 선언 +
	// unique_ptr로 Scene.h의 광범위한 소비자가 tween.hpp까지 전이 include하지 않게 한다.
	std::unique_ptr<TweenManager> m_tweenManager;

    // 레지스트리 경로의 단계 실행. 위 Awake()/Update() 등이 스위치를 보고 부른다.
    void RegistryDrainAwakeAndStart();
    // C3 완결 — RegistryTick 소멸. 틱은 전용 시스템이 돈다.
    // 트랙 C2-0으로 순회 중 발화점(CameraSystem::Update)이 되살아난 뒤로는
    // 이 함수가 "그 시스템의 vector가 비어 순회 중 지점이 아예 안 돈 경우"를
    // 잡는 폴백이다(Scene.cpp 주석 — PumpReentrancyStress 상단). phaseLabel은
    // 어느 페이즈의 폴백인지 로그에 남긴다.
    //
    // 호출부는 Update(CameraSystem 루프 직후)와 LateUpdate 둘뿐이다 — FixedUpdate에는
    // 없다. PlayerMain::Update가 매 프레임 Physics(FixedUpdate)를 GameLogic(Update·
    // LateUpdate) 앞에 무조건 부르므로, FixedUpdate에 폴백을 두면 Update의 순회 중
    // 발화점이 한 번도 돌기 전에 무장을 항상 먼저 가로챈다 — "순회 중 발화가
    // 우선"이라는 순서 규약이 깨진다(Scene.cpp의 FixedUpdate 상단 주석 참고).
    void PumpReentrancyStress(const char* phaseLabel);

public:
    //EventBroadcaster
    // 프레임 시작의 생명주기 큐 드레인. **매 프레임 돈다** — "씬을 깨운다"가 아니다
    // (트랙 C · C4에서 Awake()에서 개명). 이미 깬 컴포넌트는 플래그로 건너뛰므로
    // 같은 프레임에 여러 번 불러도 안전하다.
    //
    // 옛 이름은 실제로 판단을 방해했다: C2-2의 권고 해법이 이 함수의 동기 재호출인데
    // "씬을 다시 깨운다"로 읽혀 위험해 보였고, Api_Prefab_Instantiate가 이미 그렇게
    // 쓰고 있다는 사실을 확인하고서야 안전이 납득됐다.
    void DrainPendingLifecycle();

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

    // 프레임 끝 정리 패스. **매 프레임 돈다** — 씬이 파괴될 때 도는 것이 아니다
    // (트랙 C · C4에서 OnDestroy()에서 개명). FlushPendingDestroy가 파괴 단일점이고
    // 그 뒤 DestroyLight/Components/GameObjects가 실제 해제를 한다.
    void EndFramePass();

    void AllDestroyMark();

	static Scene* CreateNewScene(std::string_view sceneName = "SampleScene")
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = sceneName.data();
		allocScene->AddRootEntity(sceneName);
		return allocScene;
	}

	static Scene* LoadScene(std::string_view name)
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = name.data();
		return allocScene;
	}

    size_t m_buildIndex{ 0 };
	HashingString m_sceneName;
	AssetBundle m_requiredLoadAssetsBundle{};

public:
    Entity* GetSelectedEntity() { return m_selectedEntity; }
    void ResetSelectedEntity();

public:
	void CollectLightComponent(LightComponent* ptr);
	void UnCollectLightComponent(LightComponent* ptr);
	// 아래 넷은 편집기 부기다. 그리는 값은 LightRenderProxy가 든다 —
	// UpdateLight(렌더러로 가던 매 프레임 복사)는 그래서 사라졌다.
    size_t AddLight();
	void EnsureLightSlot(size_t index);
    void RemoveLight(size_t index);
	void DestroyLight();

public:
	// 렌더 계열 벡터의 getter들(GetMeshRenderers·GetSpriteRenderers 등 8종)은
	// 호출자 0으로 확인돼 걷어냈다(2026-08-20 전수 추적) — 소비는 전부
	// CommitRenderProxies가 내부에서 한다. 기즈모가 쓰는 컬라이더 getter 4종만 남는다.
	void CollectMeshRenderer(MeshRenderer* ptr);
	void UnCollectMeshRenderer(MeshRenderer* ptr);

public:
	void CollectSpriteRenderer(SpriteRenderer* ptr);
	void UnCollectSpriteRenderer(SpriteRenderer* ptr);

public:
    void CollectTerrainComponent(TerrainComponent* ptr);
    void UnCollectTerrainComponent(TerrainComponent* ptr);

public:
    void CollectFoliageComponent(FoliageComponent* ptr);
    void UnCollectFoliageComponent(FoliageComponent* ptr);

public:
	void CollectDecalComponent(DecalComponent* ptr);
	void UnCollectDecalComponent(DecalComponent* ptr);
	void CollectImageComponent(ImageComponent* ptr);
	void UnCollectImageComponent(ImageComponent* ptr);
	void CollectTextComponent(TextComponent* ptr);
	void UnCollectTextComponent(TextComponent* ptr);
	void CollectSpriteSheetComponent(SpriteSheetComponent* ptr);
	void UnCollectSpriteSheetComponent(SpriteSheetComponent* ptr);

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

	std::span<BoxColliderComponent* const> GetBoxColliderComponents() const;
	std::span<SphereColliderComponent* const> GetSphereColliderComponents() const;
	std::span<CapsuleColliderComponent* const> GetCapsuleColliderComponents() const;
	std::span<CharacterControllerComponent* const> GetCharacterControllerComponents() const;

public:
	void AddCanvas(Entity* canvas);
	void RemoveCanvas(Entity* canvas);
	// 소유가 아니라 캐시다 — 원소는 핸들이고, 쓰는 쪽이 Resolve로 그 자리에서
	// 확인한다(트랙 E5-R2). 아래 필드 선언의 주석에 사유가 있다.
	std::vector<EntityHandle>& GetCanvases() { return Canvases; }
	std::unordered_map<std::string, EntityHandle>& GetCanvasMap() { return CanvasMap; }
	// 해석까지 끝난 값을 준다(fail-closed — 이 씬에 없으면 nullptr).
	Entity* FindCanvasName(std::string_view name);

private:
    void DestroyEntities();
	void DestroyComponents();
	std::span<MeshRenderer* const> MeshRendererComponents() const;
	std::span<FoliageComponent* const> FoliageComponents() const;
	bool EnsureExecutionGraphsCompiled();
	bool CompileExecutionGraphs(uint64_t topologyVersion);
	std::string GenerateUniqueEntityName(const std::string_view& name);
	void RemoveEntityName(const std::string_view& name);
    // parentChanged: S2(dirty push / lazy pull) — 부모가 이번 순회에서 실제로
    // 바뀌었는지(재계산했는지) 자식에게 물려주는 신호. true면 이 노드는 dirty
    // 여부와 무관하게 재계산한다. 옛 이름은 recursive였고 실제로는 쓰이지 않는
    // 표지였다 — 지금은 진짜로 소비하는 값이라 이름을 바꿨다.
	struct TransformUpdateAccumulator;
	void UpdateModelRecursive(Entity::Index entityIndex, math::matrix4x4 model,
		bool parentChanged = false,
		std::unordered_set<Entity::Index>* visited = nullptr, int depth = 0,
		TransformUpdateAccumulator* diagnostics = nullptr);

	// UI 레이아웃 순회의 유일한 구현. 부모의 rect·배율·변경 여부를 받아 자신을
	// 계산하고 자식으로 내려간다(PHASE 7-5).
	//
	// isTopLevel은 "부모가 UI 좌표계를 정해 주지 않는다"는 뜻이다. 이때 캔버스라면
	// 화면 크기로 직접 구동하고(7-1), 아니면 화면 rect를 부모로 삼는다(7-2).
	// visited는 같은 노드를 두 번 계산하지 않게 막는다 — 두 번째 방문은 배율을
	// 잘못된 값으로 덮어써서 캔버스 스케일러를 무력화한다.
	void LayoutUINode(Entity* obj, const math::rect& parentRect,
		float parentScale, bool parentChanged, bool isTopLevel, int depth,
		std::unordered_set<Entity*>& visited);

	// 순회 진입 가드 단일화(SceneGraphRedesignPlan §4 트랙 S, S2) —
	// UpdateModelRecursive·LayoutUINode가 손으로 각자 구현하던 "방문 집합
	// 삽입 + 최대 깊이 검사"를 한 곳으로 모은다. 둘 다 "두 번째 방문은
	// 잘못된 문맥으로 값을 덮어쓴다"는 같은 이유로 재방문을 막고, 같은
	// kTraversalMaxDepth로 순환/과深 계층을 끊는다. 반환 false면 호출자는
	// 그 자리에서 return한다. traversalLabel은 로그 메시지 접두(예:
	// "[Transform] 월드 행렬 갱신 순회") — 원래 각 함수가 남기던 문구를 그대로
	// 보존한다.
	//
	// 인덱스 유효성·슬롯 점유·IsDestroyMark는 여기 들어오지 않는다 — 두 호출부가
	// 다루는 키 타입(인덱스 vs 이미 해석된 포인터)과 그 검사 방법이 달라 호출부가
	// 각자 먼저 확인한다. GetComponentsInChildren(Entity.inl)은 Scene.h를
	// include할 수 없어(순환 방지, Entity.inl 상단 주석) 이 헬퍼를 못 쓰고
	// 자체 가드를 갖는다. DetachEntityHierarchy는 BFS+슬롯 해제 단일점이라
	// 모양이 달라 수렴시키지 않았다 — tombstone 뒤 TryGetEntity가 이미
	// nullptr을 돌려주므로 순환이 있어도 중복 처리 없이 자연히 멈춘다(추적 확인,
	// 최종 보고 참고). 억지 수렴은 이 저장소에서 반복된 실패 양식이라 피했다.
	template<typename Key>
	static bool TryEnterTraversal(std::unordered_set<Key>& visited, const Key& key,
		int depth, const char* traversalLabel, std::string_view nodeName);

	static constexpr int kTraversalMaxDepth = 64;

private:
	void SetInternalPhysicData();

public:
	void AllUpdateWorldMatrix(
		TransformSyncPoint syncPoint = TransformSyncPoint::Unspecified);
	void AllUIUpdateWorldMatrix();
	void SyncDerivedState(
		TransformSyncPoint syncPoint = TransformSyncPoint::Unspecified);
	bool ResolveSpatialTransforms();
	bool EnsureResolved(EntityHandle target);
	AnimatorPoseUploadMetrics PublishAnimatorPose(Animator& animator);
	TransformBulkWriteMetrics ApplyWorldWriteBatch(
		std::span<const TransformWorldWrite> writes, TransformWriteReason reason);
	void MarkUILayoutDirty();
	void MarkSpatialTransformsDirty();
	static void SetSparseSpatialResolverEnabled(bool enabled)
	{
		s_sparseSpatialResolverEnabled.store(enabled, std::memory_order_release);
	}
	static bool IsSparseSpatialResolverEnabled()
	{
		return s_sparseSpatialResolverEnabled.load(std::memory_order_acquire);
	}

	// X0 계측은 명시적으로 켠 동안에만 노드별 clock/atomic 비용을 낸다.
	static void SetTransformDiagnosticsEnabled(bool enabled)
	{
		s_transformDiagnosticsEnabled.store(enabled, std::memory_order_relaxed);
	}
	static bool IsTransformDiagnosticsEnabled()
	{
		return s_transformDiagnosticsEnabled.load(std::memory_order_relaxed);
	}
	const TransformUpdateMetrics& GetLastTransformUpdateMetrics(
		TransformSyncPoint syncPoint) const;
	TransformTopologyMutationCounters GetTopologyMutationTotals() const;
	TransformTopologyMutationCounters GetTransformDiagnosticTopologyMutations() const;
	uint64_t GetTransformDiagnosticFrameCount() const
	{
		return m_transformDiagnosticFrameCount;
	}
	const TransformTopologyMutationCounters& GetLastFrameTopologyMutations() const
	{
		return m_lastFrameTopologyMutations;
	}
	void ResetTransformDiagnostics();

	// X1 reason 계측 toggle. X2부터 publication 자체는 항상 spatial epoch를 올리고,
	// 이 toggle은 reason/invalid-handle 진단 atomic만 켜고 끈다.
	static void SetTransformWriteDiagnosticsEnabled(bool enabled)
	{
		s_transformWriteDiagnosticsEnabled.store(enabled, std::memory_order_relaxed);
	}
	static bool IsTransformWriteDiagnosticsEnabled()
	{
		return s_transformWriteDiagnosticsEnabled.load(std::memory_order_relaxed);
	}
	bool PublishLocalWrite(EntityHandle handle, TransformWriteReason reason);
	uint64_t PublishLocalWriteBatch(
		std::span<const EntityHandle> handles, TransformWriteReason reason);
	TransformWriteMetrics GetTransformWriteMetrics() const;
	void ResetTransformWriteDiagnostics();

    // A/B 토글(SceneGraphRedesignPlan §4 트랙 S, S2) — dirty 인지 순회(새 경로)와
    // 항상 재계산하던 옛 경로를 같은 바이너리에서 전환한다. 기본 켬(1). 콘솔
    // scene.dirtytraversal 0|1(ConsoleCommandSystem.cpp)이 유일한 쓰기 지점이다.
    // 프로세스 전역인 이유: "지금 어느 경로로 재는가"를 씬 인스턴스와 무관하게
    // 한 곳에서 결정해야 dx12.* 류의 다른 self-test 토글들과 같은 방식으로 켜고
    // 끌 수 있다 — scene.traversalbench로 두 값을 각각 재서 비교하는 것이 이
    // 슬라이스의 측정 방법이다.
    static void SetDirtyTraversalEnabled(bool enabled) { s_dirtyTraversalEnabled = enabled; }
    static bool IsDirtyTraversalEnabled() { return s_dirtyTraversalEnabled; }

    // A/B 토글(트랙 E, E7-b) — recursive fallback의 뼈 인덱스 캐시와 매 프레임
    // Skeleton::FindBone을 다시 도는 옛 경로를 같은 바이너리에서 전환한다.
    // X7 packed resolver는 이 토글과 무관하게 binding pass에서만 해석한다.
    // 기본 켬(1). 콘솔 scene.bonecache 0|1이 유일한 쓰기 지점이다.
    //
    // 끄면 UpdateModelRecursive의 Bone 분기가 캐시 적중 여부를 묻지 않고 항상
    // FindBone(m_bones 선형 탐색 + 뼈마다 문자열 비교, RenderEngine/Skeleton.cpp:86)
    // 을 다시 돈다 — E7-b 이전과 정확히 같은 일이다. 켠 값과 끈 값을
    // scene.traversalbench 0 <프레임>으로 각각 재서 비교하는 것이 이 슬라이스의
    // 측정 방법이다(S2의 scene.dirtytraversal과 같은 방식).
    static void SetBoneCacheEnabled(bool enabled) { s_boneCacheEnabled = enabled; }
    static bool IsBoneCacheEnabled() { return s_boneCacheEnabled; }

	// UI 레이아웃 전체를 한 번에 갱신한다(PHASE 7-5).
	//
	// 예전에는 같은 일이 세 군데에 흩어져 있었다 — UpdateModelRecursive의 UI 분기,
	// UpdateUIRecursive, 그리고 UpdateLayout 안의 자식 재귀. 어느 것이 언제 몇 번
	// 도는지 추론할 수 없었고, 앞의 둘이 병렬로 돌면서 세 번째의 재귀를 호출해
	// 교차 스레드로 같은 노드를 건드릴 수 있었다(분석 문서 F-9).
	//
	// 레이아웃은 부모→자식 의존 사슬이라 병렬화할 대상이 아니다. 여기서 직렬로
	// 한 번 돌고, 트랜스폼 행렬 갱신(비-UI)만 예전처럼 병렬로 남긴다.
	bool UpdateUILayout();

	// 한 서브트리만 즉시 레이아웃한다. 프레임 패스를 기다릴 수 없는 곳
	// (에디터 드래그, UI 생성 직후)에서 쓴다. 순회 규칙은 프레임 패스와 공유한다.
	void LayoutUISubtree(Entity* root);

private:
	static constexpr size_t kTransformSyncPointCount =
		static_cast<size_t>(TransformSyncPoint::Count);
	static constexpr size_t TransformSyncPointIndex(TransformSyncPoint syncPoint)
	{
		return static_cast<size_t>(syncPoint);
	}
	void CaptureTransformSceneCensus(TransformUpdateMetrics& metrics) const;
	void RecordTopologyCreated();
	void RecordTopologyDestroyed();
	void RecordTopologyReparented();
	void PublishTopologyMutation();
	void EnterHierarchyBulkBuild();
	void ExitHierarchyBulkBuild();
	bool ResolveSpatialTransforms(
		TransformUpdateAccumulator* diagnostics, TransformUpdateMetrics* metrics);
	bool ResolveSpatialTransformsLegacy(uint64_t dirtyEpoch,
		TransformUpdateAccumulator* diagnostics, TransformUpdateMetrics* metrics,
		SpatialResolveMetrics& sparseMetrics);
	bool ResolveSpatialTransformsSparse(uint64_t dirtyEpoch,
		std::vector<EntityHandle> dirtyRoots, bool forceFull,
		TransformUpdateAccumulator* diagnostics, TransformUpdateMetrics* metrics,
		SpatialResolveMetrics& sparseMetrics);
	uint64_t TakeSpatialDirtySnapshot(
		std::vector<EntityHandle>& dirtyRoots, bool& forceFull);

    // scene.dirtytraversal 콘솔 토글의 저장소 (위 IsDirtyTraversalEnabled 참고).
    // C++17 inline 정적 멤버 — 별도 .cpp 정의가 필요 없다.
    static inline bool s_dirtyTraversalEnabled = true;

    // scene.bonecache 콘솔 토글의 저장소 (위 IsBoneCacheEnabled 참고).
    static inline bool s_boneCacheEnabled = true;
	static inline std::atomic_bool s_transformDiagnosticsEnabled = false;
	static inline std::atomic_bool s_transformWriteDiagnosticsEnabled = false;
	static inline std::atomic_bool s_sparseSpatialResolverEnabled = true;

	std::array<TransformUpdateMetrics, kTransformSyncPointCount>
		m_transformUpdateMetrics{};
	std::atomic<uint64_t> m_topologyCreated{ 0 };
	std::atomic<uint64_t> m_topologyDestroyed{ 0 };
	std::atomic<uint64_t> m_topologyReparented{ 0 };
	std::atomic<uint64_t> m_topologyVersion{ 0 };
	uint32_t m_hierarchyBulkBuildDepth = 0;
	bool m_hierarchyBulkBuildMutated = false;
	TransformTopologyMutationCounters m_topologyFrameBaseline{};
	TransformTopologyMutationCounters m_topologyObservationBaseline{};
	TransformTopologyMutationCounters m_lastFrameTopologyMutations{};
	uint64_t m_transformDiagnosticFrameCount = 0;
	std::atomic<uint64_t> m_transformPublishEpoch{ 0 };
	std::atomic<uint64_t> m_transformInvalidPublishCount{ 0 };
	std::array<std::atomic<uint64_t>, kTransformWriteReasonCount>
		m_transformWriteReasonCounts{};
	uint64_t m_transformWriteEpochBaseline = 0;
	uint64_t m_transformInvalidPublishBaseline = 0;
	std::array<uint64_t, kTransformWriteReasonCount>
		m_transformWriteReasonBaselines{};
	std::atomic<uint64_t> m_uiDirtyEpoch{ 1 };
	std::atomic<uint64_t> m_uiResolvedEpoch{ 0 };
	std::atomic<uint64_t> m_spatialDirtyEpoch{ 1 };
	std::atomic<uint64_t> m_spatialResolvedEpoch{ 0 };
	math::rect m_lastUILayoutScreenRect{};
	bool m_hasLastUILayoutScreenRect = false;

    std::unordered_set<std::string> m_entityNameSet{};

private:
	friend class PhysicsManager;
	using RigidBodyTypeLinkCallback = std::unordered_map<Entity*, std::function<void(const EBodyType&)>>;
	using ColliderContainerType = std::unordered_map<PhysicsManager::ColliderID, PhysicsManager::ColliderInfo>;

	std::vector<RigidBodyComponent*>            m_rigidBodyComponents;
	std::vector<BoxColliderComponent*>          m_boxColliderComponents;
	std::vector<SphereColliderComponent*>       m_sphereColliderComponents;
	std::vector<CapsuleColliderComponent*>      m_capsuleColliderComponents;
	std::vector<MeshColliderComponent*>         m_meshColliderComponents;
	std::vector<CharacterControllerComponent*>  m_characterControllerComponents;
	// m_terrainColliderComponents는 쓰기 전용(getter도 읽기도 없음)이라 걷어냈다 —
	// 실제 물리 등록은 PhysicsManagers->AddCollider와 m_colliderContainer가 한다.
    RigidBodyTypeLinkCallback					m_ColliderTypeLinkCallback;
	ColliderContainerType						m_colliderContainer;

private:
	// 이 씬에 속한 캔버스의 캐시. 소유가 아니다 — 수명은 m_Entities가 쥔다.
	//
	// ── weak_ptr가 아니라 EntityHandle인 이유 (트랙 E5-R2) ──
	//
	// weak_ptr는 **정체성 기반**이라 "이 C++ 객체가 살아 있는가"만 답한다. 그런데
	// 이 목록이 물어야 하는 것은 "이 캔버스가 **이 씬에** 속하는가"다. 둘이
	// 갈라지는 실경로가 있다: DontDestroyOnLoad 이송
	// (Scene::DetachEntityHierarchy)은 오브젝트를 **살려 둔 채** 슬롯만 놓는다.
	// 그래서 weak_ptr였을 때는 떠난 캔버스의 .lock()이 계속 성공해 이 목록에
	// 유령으로 남았다 — RemoveCanvas는 UIManager::DeleteCanvas(진짜 파괴)에서만
	// 불리므로 아무도 지우지 않는다.
	//
	// EntityHandle이면 그 자리에서 fail-closed가 된다. ReleaseSlot이 세대를 올리고
	// (Scene.cpp의 슬롯 해제 단일점) Resolve가 sceneId·세대 둘 다 확인하므로,
	// 이송된 캔버스는 다음 해석에서 조용히 걸러진다. 등록 이벤트를 놓쳐도
	// 안전한 방향으로만 틀린다는 것이 이 캐시의 계약이다.
	std::vector<EntityHandle>	Canvases;
	std::unordered_map<std::string, EntityHandle> CanvasMap;

public:
	// 이 씬의 EntityHandle::sceneId (트랙 W). Resolve/HandleOf가 내부적으로
	// 쓰는 값과 같다 — 외부 소비자가 핸들을 직접 짜맞출 일이 생기면(현재는
	// 없다) 여기서 얻는다.
	uint32_t GetSceneId() const { return m_sceneId; }
	HashingString GetSceneName() const { return m_sceneName; }
    Entity*					m_selectedEntity = nullptr;
	std::vector<Entity*>	m_selectedEntities;
    Core::DelegateHandle		resetObjHandle{};
};
