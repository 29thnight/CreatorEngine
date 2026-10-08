#pragma once
#include "Ownership.h"
#include "Core.Minimal.h"
#include <vector>
#include <mutex>
#include <thread>
#include <variant>
#include "../RenderEngine/AssetDepot/AssetLink.h"
#include "../RenderEngine/AssetDepot/AssetRequest.h"

class Entity;
class Texture;
class DataSystem;
namespace material_graph { struct Generation; }

// Closed concrete-type registration for this ABI adapter, not manifest kinds.
// Several native types share a manifest kind (for example ::Material and
// experiment::Material); a kind alone never proves which C++ owner a slot holds.
// Zero is deliberately unregistered. Preserve Texture's existing v34 token ID.
template<class T> inline constexpr std::uint32_t kScriptAssetConcreteType = 0u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<Texture> = 3u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<assets::ModelAnimationDescriptor> = 0x00010001u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<assets::ModelMeshDescriptor> = 0x00010002u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<assets::ModelSkeletonPayload> = 0x00010003u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<assets::ModelAnimationPayload> = 0x00010004u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<ShaderMeta> = 0x00010005u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<material_graph::Generation> = 0x00010006u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<::Material> = 0x00010007u;
inline constexpr std::uint32_t kScriptAssetRequestBit = 0x80000000u;

// 관리 코드에 넘기는 객체 핸들.
//
// 관리 코드는 네이티브 포인터의 유효성을 알 수 없다. 슬롯을 재사용할 때 세대를 올려두면
// 파괴된 객체를 가리키던 핸들이 다음 조회에서 자동으로 걸러진다.
// C# 측 ObjectHandle과 배치가 같아야 한다(둘 다 uint32 두 개).
//
// ── EntityHandle과 배치는 같지만 값은 별개다(SceneGraphRedesignPlan 트랙 E4) ──
//
// "C#에 넘기는 핸들이 곧 엔진 핸들"이 트랙 E4의 목표 문구였지만, 이 핸들의 세대를
// Scene::m_generations(EntityHandle의 세대)로 그대로 대체하는 시도는 코드 추적으로
// 기각했다 — DontDestroyOnLoad 이송 창(Scene::DetachEntityHierarchy가 슬롯을
// 먼저 놓고 AttachExistingEntity*가 나중에 다시 잡는 구간) 동안 그 오브젝트는
// 어느 씬의 슬롯에도 없다. 그런데 바로 이 구간에서 실제로 관리 코드가 도는 지점이
// 있다 — SceneManager::LoadSceneImmediate가 Detach 직후·재부착 이전에
// ClrHost::NotifySceneUnload를 부르고, 그 안에서 ScriptRegistry.SweepOrphans가
// 모든 활성 스크립트의 Entity.IsAlive를 확인한다(주석에 "살아 있다 — DDOL
// 포함"이라고 명시되어 있다 — ScriptCore/ScriptRegistry.cs:324). 세대 판정을
// Scene에 위임했다면 이 순간 DDOL 오브젝트가 전부 "죽었다"로 오판되어
// SweepOrphans가 살아있는 스크립트를 매 씬 전환마다 뜯어냈을 것이다 — 자가
// 회귀가 아니라 최초 설계 검토에서 걸러낸 함정이다.
//
// 그래서 이 레지스트리는 자기 세대를 계속 갖는다 — 다만 그 세대를 올리는 지점은
// 하나로 수렴했다: Entity::Destroy()(ScriptObjectRegistry.cpp의 Register/
// Unregister 주석, Entity.cpp:Destroy 참고). Destroy()는 자식까지 재귀하는
// 유일한 진짜 파괴 API이고, DDOL 이송(DetachEntityHierarchy)은 그 경로를
// 타지 않는다 — 그래서 세대가 Scene 것과 별개여도 "진짜 파괴"와 "일시적 씬
// 이탈"을 정확히 가른다. 씬 자체가 통째로 헐릴 때는 Clear()(ClrHost.cpp의
// NotifySceneUnload)가 전 슬롯을 무효화하는 기존 동작을 그대로 유지한다(§1.4
// 보존 대상 — "씬 언로드 경로의 안전성"). 값이 같지 않을 뿐 개념은 여전히
// EntityHandle과 같다 — 슬롯 인덱스 + 세대, 0은 무효.
struct ScriptObjectHandle
{
	uint32_t index{ 0 };
	uint32_t generation{ 0 };   // 0 = 무효

	bool IsValid() const { return generation != 0; }
};

// Flat AssetDepot ABI values. No C++ owner, string, job or resource pointer crosses CLR.
struct ScriptAssetId
{
    std::uint64_t first{};
    std::uint64_t second{};
};

struct ScriptAssetLink
{
    ScriptAssetId asset{};
    ScriptAssetId subasset{};
    std::uint32_t kind{};
    std::uint32_t reserved{};
};

struct ScriptAssetToken
{
    std::uint32_t index{};
    std::uint32_t generation{};
    // Concrete native type proof, independent of ScriptAssetLink::kind. The high
    // bit distinguishes consumer requests from strong owners of that exact type.
    std::uint32_t type{};
};

struct ScriptTextureAssetVariant
{
    std::uint32_t colorSpace{};
    std::uint32_t compress{};
    std::uint32_t role{};
};

struct ScriptAssetRequestSnapshot
{
    std::int32_t status{};
    std::int32_t error{};
    std::int32_t workComplete{};
    std::int32_t messageBytes{};
};

struct ScriptTextureDescriptor
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mipLevels{};
    std::uint32_t arraySize{};
    std::uint32_t isCube{};
};

static_assert(sizeof(ScriptAssetLink) == 40);
static_assert(sizeof(ScriptAssetToken) == 12);
static_assert(sizeof(ScriptTextureAssetVariant) == 12);
static_assert(sizeof(ScriptAssetRequestSnapshot) == 16);
static_assert(sizeof(ScriptTextureDescriptor) == 20);

enum class ScriptAssetResult : std::int32_t
{
    Success,
    Unavailable,
    InvalidToken,
    InvalidLink,
    UnsupportedType,
    WrongThread,
    InvalidArgument,
    NotResident,
    InternalError,
};

// 스크립트가 참조하는 GameObject만 담는 슬롯 테이블.
//
// 씬의 모든 오브젝트를 넣지 않는다 — 스크립트가 실제로 잡고 있는 것만 등록하므로
// 규모가 작고, 조회는 인덱스 한 번이다.
class ScriptObjectRegistry
{
public:
	static ScriptObjectRegistry& Get();

	// 이미 등록된 객체면 기존 핸들을 그대로 돌려준다. 역방향 map 없이(트랙 E4)
	// m_slots를 선형 탐색해 같은 포인터를 찾는다 — 클래스 선언부의 "규모가 작다"는
	// 전제를 그대로 이용한 교환이다: 해시맵 하나를 지우는 대가로 등록마다 O(n)
	// 탐색을 받아들인다. n이 "스크립트가 실제로 잡고 있는 것"으로 제한되는 한
	// 값싸다.
	ScriptObjectHandle Register(Entity* object);

	// 객체가 파괴될 때 부른다. 세대가 올라가 기존 핸들이 전부 무효가 된다.
	// 정본 호출 지점은 Entity::Destroy() 단 하나다(트랙 E4 — 위 주석 참고).
	// 재귀 파괴(부모→자식)도 전부 Destroy()를 거치므로 이 한 지점 밖에서 객체가
	// 사라지는 경로가 없다 — N-4(엔진 주도 파괴가 레지스트리를 비껴가던 문제)가
	// 구조적으로 재발 불가능해지는 지점이 여기다.
	void Unregister(Entity* object);

	// 세대가 어긋나면 nullptr. 이 검사 하나가 UAF를 구조적으로 막는다.
	Entity* Resolve(ScriptObjectHandle handle) const;

	void Clear();
	size_t LiveCount() const;

    // Minimal ownership adapter in the existing opaque registry. The host owns
    // its GT session; shutdown invalidates tokens without touching DataSystem.
    void BeginAssetSession(DataSystem* dataSystem);
    void EndAssetSession();
    ScriptAssetResult RequestAsset(const ScriptAssetLink& link, const ScriptTextureAssetVariant& variant,
        bool residentOnly, ScriptAssetToken& token);
    ScriptAssetResult SnapshotAssetRequest(ScriptAssetToken token, ScriptAssetRequestSnapshot& snapshot,
        char* message, int capacity);
    ScriptAssetResult AcquireAssetResult(ScriptAssetToken request, ScriptAssetToken& owner);
    ScriptAssetResult CancelAssetRequest(ScriptAssetToken request);
    ScriptAssetResult ReleaseAsset(ScriptAssetToken token);
    ScriptAssetResult ReadTexture(ScriptAssetToken token, ScriptTextureDescriptor& descriptor);
    ScriptAssetResult ListAssetRoots(std::uint64_t mount, std::uint32_t kind,
        ScriptAssetLink* links, int capacity, int& count);

private:
    // These are the actual native owners/consumer requests, never void owners,
    // adopted pointers, a second AssetPtr wrapper or a polymorphic asset object.
    using AssetStorage = std::variant<std::monostate,
        own::shared_owner<const Texture>,
        own::shared_owner<const assets::ModelAnimationDescriptor>,
        own::shared_owner<const assets::ModelMeshDescriptor>,
        own::shared_owner<const assets::ModelSkeletonPayload>,
        own::shared_owner<const assets::ModelAnimationPayload>,
        own::shared_owner<const ShaderMeta>,
        own::shared_owner<const material_graph::Generation>,
        own::shared_owner<const ::Material>,
        AssetDepot::AssetRequest<Texture>,
        AssetDepot::AssetRequest<assets::ModelAnimationDescriptor>,
        AssetDepot::AssetRequest<assets::ModelMeshDescriptor>,
        AssetDepot::AssetRequest<assets::ModelSkeletonPayload>,
        AssetDepot::AssetRequest<assets::ModelAnimationPayload>,
        AssetDepot::AssetRequest<ShaderMeta>,
        AssetDepot::AssetRequest<material_graph::Generation>,
        AssetDepot::AssetRequest<::Material>>;

    struct AssetSlot
    {
        std::uint32_t generation{ 1u };
        std::uint32_t type{};
        AssetStorage value{};
    };

    [[nodiscard]] ScriptAssetResult CheckAssetSessionLocked() const;
    [[nodiscard]] AssetSlot* FindAssetLocked(ScriptAssetToken token);
    [[nodiscard]] ScriptAssetToken InsertAssetLocked(AssetStorage value);
    template<class T>
    ScriptAssetResult RequestTypedAssetLocked(const ScriptAssetLink& link,
        const ScriptTextureAssetVariant& variant, bool residentOnly, ScriptAssetToken& token);
    template<class T>
    ScriptAssetResult ListTypedAssetRootsLocked(std::uint64_t mount,
        ScriptAssetLink* links, int capacity, int& count);
    void ReleaseAssetLocked(AssetSlot& slot);
    std::mutex m_assetMutex;
    std::vector<AssetSlot> m_assetSlots;
    DataSystem* m_assetDataSystem{};
    std::thread::id m_assetThread{};
    bool m_assetsActive{};

	struct Slot
	{
		Entity* object{ nullptr };
		uint32_t generation{ 0 };
	};

	// AI 잡 스레드(Scene::m_AIJob) 문제로 뮤텍스를 남겼다 — 정확히는, 코드
	// 추적으로 QueueAITick/FlushAITicks 경로(BehaviorTreeComponent::InternalAIUpdate)
	// 는 이 레지스트리를 건드리지 않는다는 것까지는 확인했다(int/float만 담아
	// SpinLock으로 보호되는 별도 큐로 넘긴다). ClrHost.h의 "관리 코드 호출은
	// 게임 스레드로 한정한다" 불변식과 합치면 제거해도 될 근거가 있지만, 그
	// 근거는 정적 추적일 뿐 실측(빌드·동시성 스트레스)이 아니다 — 이번 슬라이스는
	// 빌드가 금지라 실측할 수 없었다. "필요 시 유지" 쪽으로 보수적으로 남긴다.
	mutable std::mutex m_mutex;
	std::vector<Slot> m_slots;
	std::vector<uint32_t> m_freeSlots;
};
