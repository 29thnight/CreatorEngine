#include "../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.SpinLock.h"
#else
import Creator.Core.SpinLock;
#endif
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "../RenderEngine/AssetDepot/AssetLink.h"
#include "../RenderEngine/AssetDepot/AssetRequest.h"
#include "ScriptBindingValues.h"

class Entity;
class Texture;
class DataSystem;
namespace material_graph
{
    struct Generation;
}
namespace LX::Runtime
{
    struct ShaderGeneration;
}

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
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<LX::Runtime::ShaderGeneration> = 0x00010008u;
template<> inline constexpr std::uint32_t kScriptAssetConcreteType<experiment::Material> = 0x00010009u;
inline constexpr std::uint32_t kScriptAssetRequestBit = 0x80000000u;

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
    // ABI v35 selects the concrete runtime view separately from stable link data.
    ScriptAssetResult RequestAssetTyped(const ScriptAssetLink& link, std::uint32_t concreteType,
        const ScriptTextureAssetVariant& variant, bool residentOnly, ScriptAssetToken& token);
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
        AssetDepot::AssetRequest<::Material>,
        own::shared_owner<const LX::Runtime::ShaderGeneration>,
        own::shared_owner<const experiment::Material>,
        AssetDepot::AssetRequest<LX::Runtime::ShaderGeneration>,
        AssetDepot::AssetRequest<experiment::Material>>;

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
    ce::dynamic_array<AssetSlot> m_assetSlots;
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
	// spin_lock으로 보호되는 별도 큐로 넘긴다). ClrHost.h의 "관리 코드 호출은
	// 게임 스레드로 한정한다" 불변식과 합치면 제거해도 될 근거가 있지만, 그
	// 근거는 정적 추적일 뿐 실측(빌드·동시성 스트레스)이 아니다 — 이번 슬라이스는
	// 빌드가 금지라 실측할 수 없었다. "필요 시 유지" 쪽으로 보수적으로 남긴다.
	mutable std::mutex m_mutex;
	ce::dynamic_array<Slot> m_slots;
	ce::dynamic_array<uint32_t> m_freeSlots;
};
