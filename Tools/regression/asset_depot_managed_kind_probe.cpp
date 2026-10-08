// Unrun source fixture for a dedicated engine test host. Not a standalone build
// target: include this source in a harness with a source-free v3 mount containing
// at least one root of every registered kind, plus graph/code programs and
// graph/authored materials. Supply their exact fixture links to Begin; kind-only
// enumeration does not promise representation compatibility. Tick Poll on the GT
// while the normal scheduler runs, then VerifySessionRestart. Do not block a GT.
#include "../../Engine/SceneRuntime/ScriptObjectRegistry.h"
#include "../../Engine/RenderEngine/DataSystem.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace script_asset_probe
{
    using Kind = experiment::cooked::CookedAssetKind;
    static_assert(sizeof(ScriptAssetToken) == 12u && sizeof(ScriptAssetLink) == 40u);
    static_assert(AssetDepot::AssetLink<::Material>::kKind == AssetDepot::AssetLink<experiment::Material>::kKind);
    static_assert(kScriptAssetConcreteType<::Material> != 0u);
    static_assert(kScriptAssetConcreteType<experiment::Material> != 0u);
    static_assert(kScriptAssetConcreteType<experiment::Material> != kScriptAssetConcreteType<::Material>);
    static_assert(AssetDepot::AssetLink<material_graph::Generation>::kKind
        == AssetDepot::AssetLink<LX::Runtime::ShaderGeneration>::kKind);
    static_assert(kScriptAssetConcreteType<material_graph::Generation>
        != kScriptAssetConcreteType<LX::Runtime::ShaderGeneration>);
    static_assert(kScriptAssetConcreteType<assets::ModelAssetGeneration> == 0u);
    static_assert(kScriptAssetConcreteType<assets::ModelGeometryPayload> == 0u);
    static_assert(kScriptAssetConcreteType<assets::ModelAnimationDescriptor>
        != static_cast<std::uint32_t>(Kind::Model));

    struct Case
    {
        std::uint32_t kind{};
        std::uint32_t concreteType{};
        ScriptAssetLink link{};
        ScriptAssetToken request{};
        ScriptAssetToken owner{};
        bool codeRepresentation{};
    };

    struct MaterialRoots
    {
        ScriptAssetLink graphProgram{};
        ScriptAssetLink codeProgram{};
        ScriptAssetLink graphMaterial{};
        ScriptAssetLink authoredMaterial{};
    };

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    // Source-only coverage for consumer accounting; this function has not run.
    void VerifyRequestAccounting()
    {
        auto counters = own::make_shared<AssetDepot::AssetRequestCounters>();
        {
            auto consumer = own::make_shared<AssetDepot::AssetRequestState<Texture>>(counters);
            auto copiedOwner = consumer;
            Require(counters->Snapshot().created == 1u && counters->Snapshot().pendingConsumers == 1u,
                "Handle copies must not count as separate consumers");
            std::lock_guard lock(consumer->mutex);
            consumer->SetTerminalLocked(AssetDepot::AssetRequestStatus::Failed);
            consumer->SetTerminalLocked(AssetDepot::AssetRequestStatus::Cancelled);
            Require(counters->Snapshot().failed == 1u && counters->Snapshot().cancelled == 0u,
                "A terminal consumer outcome was counted twice");
        }
        {
            auto abandoned = own::make_shared<AssetDepot::AssetRequestState<Texture>>(counters);
        }
        {
            auto cancelled = own::make_shared<AssetDepot::AssetRequestState<Texture>>(counters);
            std::lock_guard lock(cancelled->mutex);
            cancelled->SetTerminalLocked(AssetDepot::AssetRequestStatus::Cancelled);
        }
        const auto result = counters->Snapshot();
        Require(result.created == 3u && result.liveConsumers == 0u && result.pendingConsumers == 0u
            && result.failed == 1u && result.cancelled == 1u && result.abandonedPending == 1u,
            "Request lifetime/outcome accounting differs");
        std::atomic<std::size_t> observed{};
        {
            AssetDepot::AssetByteObservation first(observed);
            first.Set(128u);
            {
                AssetDepot::AssetByteObservation second(observed);
                second.Set(64u);
                first.Set(32u);
                Require(observed.load() == 96u, "Concurrent staging observations were not combined");
            }
            Require(observed.load() == 32u, "Staging scope did not release its own charge");
        }
        Require(observed.load() == 0u, "Staging charge survived its buffers");
    }

    class Driver final
    {
    public:
        Driver() = default;
        Driver(const Driver&) = delete;
        Driver& operator=(const Driver&) = delete;
        ~Driver()
        {
            if (m_started)
            {
                ScriptObjectRegistry::Get().EndAssetSession();
            }
        }

        void Begin(DataSystem& dataSystem, std::uint64_t mount, const MaterialRoots& materials)
        {
            auto& registry = ScriptObjectRegistry::Get();
            Require(!m_started, "The fixture must start once in a dedicated host");
            registry.BeginAssetSession(&dataSystem);
            m_started = true;
            for (auto& item : m_cases)
            {
                int count{};
                Require(registry.ListAssetRoots(mount, item.kind, nullptr, 0, count)
                    == ScriptAssetResult::Success && count > 0, "Missing typed fixture root");
                std::vector<ScriptAssetLink> roots(static_cast<std::size_t>(count));
                Require(registry.ListAssetRoots(mount, item.kind, roots.data(), count, count)
                    == ScriptAssetResult::Success, "Typed metadata enumeration failed");
                item.link = roots.front();
                if (item.kind == static_cast<std::uint32_t>(Kind::MaterialProgram))
                {
                    item.link = item.codeRepresentation ? materials.codeProgram : materials.graphProgram;
                }
                else if (item.kind == static_cast<std::uint32_t>(Kind::Material))
                {
                    item.link = item.codeRepresentation ? materials.authoredMaterial : materials.graphMaterial;
                }
                Require(item.link.kind == item.kind && item.link.reserved == 0u, "Root kind mismatch");

                ScriptAssetToken rejected{};
                auto malformed = item.link;
                malformed.reserved = 1u;
                Require(registry.RequestAssetTyped(malformed, item.concreteType, {}, false, rejected) == ScriptAssetResult::InvalidLink
                    && rejected.generation == 0u, "Reserved link field accepted");
                malformed = item.link;
                malformed.kind |= 0x100u;
                Require(registry.RequestAssetTyped(malformed, item.concreteType, {}, false, rejected) == ScriptAssetResult::InvalidLink,
                    "Manifest kind was truncated before validation");
                if (item.kind != static_cast<std::uint32_t>(Kind::Texture))
                {
                    Require(registry.RequestAssetTyped(item.link, item.concreteType, { 1u, 0u, 0u }, false, rejected)
                        == ScriptAssetResult::InvalidArgument, "Non-texture options ignored");
                    Require(registry.RequestAssetTyped(item.link, item.concreteType,
                        { 0u, 0u, kScriptAssetConcreteType<experiment::Material> }, false, rejected)
                        == ScriptAssetResult::InvalidArgument, "Texture role was used as a concrete-view selector");
                }

                Require(registry.RequestAssetTyped(item.link, 0u, {}, false, rejected)
                    == ScriptAssetResult::UnsupportedType && rejected.generation == 0u, "Zero selector accepted");
                Require(registry.RequestAssetTyped(item.link, item.concreteType | kScriptAssetRequestBit,
                    {}, false, rejected) == ScriptAssetResult::UnsupportedType && rejected.generation == 0u,
                    "Request-role bits accepted as a concrete-view selector");
                Require(registry.RequestAssetTyped(item.link, 0x00020008u, {}, false, rejected)
                    == ScriptAssetResult::UnsupportedType && rejected.generation == 0u, "Unknown selector accepted");
                malformed = item.link;
                malformed.kind = item.kind == static_cast<std::uint32_t>(Kind::Texture)
                    ? static_cast<std::uint32_t>(Kind::Material) : static_cast<std::uint32_t>(Kind::Texture);
                Require(registry.RequestAssetTyped(malformed, item.concreteType, {}, false, rejected)
                    == ScriptAssetResult::InvalidLink && rejected.generation == 0u, "Concrete view accepted another link kind");

                // The retained v34 entry always selects its original view, even
                // for a code/authored representation of the same manifest kind.
                ScriptAssetToken legacy{};
                Require(registry.RequestAsset(item.link, {}, false, legacy) == ScriptAssetResult::Success,
                    "Legacy request entry disappeared");
                auto legacyType = item.kind == static_cast<std::uint32_t>(Kind::MaterialProgram)
                    ? kScriptAssetConcreteType<material_graph::Generation>
                    : item.kind == static_cast<std::uint32_t>(Kind::Material)
                        ? kScriptAssetConcreteType<::Material> : item.concreteType;
                Require(legacy.type == (legacyType | kScriptAssetRequestBit), "Legacy kind dispatch changed meaning");
                Require(registry.ReleaseAsset(legacy) == ScriptAssetResult::Success, "Legacy request release failed");

                ScriptAssetToken cancelled{};
                Require(registry.RequestAssetTyped(item.link, item.concreteType, {}, false, cancelled) == ScriptAssetResult::Success,
                    "First consumer request failed");
                Require(registry.RequestAssetTyped(item.link, item.concreteType, {}, false, item.request) == ScriptAssetResult::Success,
                    "Second consumer request failed");
                Require(cancelled.index != item.request.index, "Consumers share one native slot");
                Require(item.request.type == (item.concreteType | kScriptAssetRequestBit), "Wrong concrete request type");
                Require(registry.CancelAssetRequest(cancelled) == ScriptAssetResult::Success, "Cancel failed");
                Require(registry.ReleaseAsset(cancelled) == ScriptAssetResult::Success, "Request release failed");
                Require(registry.ReleaseAsset(cancelled) == ScriptAssetResult::InvalidToken, "Duplicate release accepted");
            }
            Require(registry.RequestAssetTyped(materials.graphProgram,
                kScriptAssetConcreteType<LX::Runtime::ShaderGeneration>, {}, false, m_wrongCodeProgram)
                == ScriptAssetResult::Success, "Wrong-representation request was not created");
            Require(registry.RequestAssetTyped(materials.codeProgram,
                kScriptAssetConcreteType<material_graph::Generation>, {}, false, m_wrongGraphProgram)
                == ScriptAssetResult::Success, "Reverse wrong-representation request was not created");
            Require(registry.RequestAssetTyped(materials.graphMaterial,
                kScriptAssetConcreteType<experiment::Material>, {}, false, m_wrongAuthoredMaterial)
                == ScriptAssetResult::Success, "Wrong authored-material representation request was not created");
        }

        // Return to the normal GT/scheduler between calls. Failed/cancelled/stale
        // siblings are errors, not permission to call a pending request complete.
        bool Poll()
        {
            auto& registry = ScriptObjectRegistry::Get();
            bool complete = true;
            for (auto& item : m_cases)
            {
                if (item.owner.generation != 0u)
                {
                    continue;
                }
                ScriptAssetRequestSnapshot snapshot{};
                Require(registry.SnapshotAssetRequest(item.request, snapshot, nullptr, 0)
                    == ScriptAssetResult::Success, "Request snapshot failed");
                if (snapshot.status == static_cast<std::int32_t>(AssetDepot::AssetRequestStatus::Pending))
                {
                    complete = false;
                    continue;
                }
                Require(snapshot.status == static_cast<std::int32_t>(AssetDepot::AssetRequestStatus::Ready),
                    "Consumer cancellation leaked or cooked runtime acquisition failed");
                Require(registry.AcquireAssetResult(item.request, item.owner) == ScriptAssetResult::Success,
                    "Independent strong owner acquire failed");
                ScriptAssetToken second{};
                Require(registry.AcquireAssetResult(item.request, second) == ScriptAssetResult::Success,
                    "Repeated owner acquire failed");
                Require(second.index != item.owner.index && second.type == item.concreteType
                    && item.owner.type == item.concreteType, "Result owners share a slot or wrong type");

                if (item.kind == static_cast<std::uint32_t>(Kind::MaterialProgram)
                    || item.kind == static_cast<std::uint32_t>(Kind::Material))
                {
                    const auto otherType = item.kind == static_cast<std::uint32_t>(Kind::MaterialProgram)
                        ? (item.concreteType == kScriptAssetConcreteType<material_graph::Generation>
                            ? kScriptAssetConcreteType<LX::Runtime::ShaderGeneration>
                            : kScriptAssetConcreteType<material_graph::Generation>)
                        : (item.concreteType == kScriptAssetConcreteType<::Material>
                            ? kScriptAssetConcreteType<experiment::Material> : kScriptAssetConcreteType<::Material>);
                    auto sameKindOwner = item.owner;
                    sameKindOwner.type = otherType;
                    Require(registry.ReleaseAsset(sameKindOwner) == ScriptAssetResult::InvalidToken,
                        "Same-kind forged concrete view released an owner");
                    auto sameKindRequest = item.request;
                    sameKindRequest.type = otherType | kScriptAssetRequestBit;
                    ScriptAssetToken rejected{};
                    Require(registry.AcquireAssetResult(sameKindRequest, rejected) == ScriptAssetResult::InvalidToken
                        && rejected.generation == 0u, "Same-kind forged request acquired another native type");
                }

                auto forged = item.owner;
                forged.type = item.concreteType == kScriptAssetConcreteType<Texture>
                    ? kScriptAssetConcreteType<::Material> : kScriptAssetConcreteType<Texture>;
                Require(registry.ReleaseAsset(forged) == ScriptAssetResult::InvalidToken, "Forged concrete type released an owner");
                forged = item.owner;
                forged.type |= kScriptAssetRequestBit;
                Require(registry.CancelAssetRequest(forged) == ScriptAssetResult::InvalidToken, "Owner accepted as request");
                ScriptTextureDescriptor description{};
                Require(registry.ReadTexture(item.owner, description) ==
                    (item.kind == static_cast<std::uint32_t>(Kind::Texture)
                        ? ScriptAssetResult::Success : ScriptAssetResult::InvalidToken), "Wrong typed descriptor read");

                Require(registry.ReleaseAsset(item.request) == ScriptAssetResult::Success, "Request disposal failed");
                Require(registry.ReleaseAsset(second) == ScriptAssetResult::Success, "Second owner disposal failed");
                ScriptAssetToken resident{};
                Require(registry.RequestAssetTyped(item.link, item.concreteType, {}, true, resident) == ScriptAssetResult::Success,
                    "First result owner did not preserve resident generation after request disposal");
                Require(registry.ReleaseAsset(second) == ScriptAssetResult::InvalidToken, "Reused slot accepted a stale token");
                Require(registry.ReleaseAsset(resident) == ScriptAssetResult::Success, "Resident owner release failed");
            }
            for (auto* request : { &m_wrongCodeProgram, &m_wrongGraphProgram, &m_wrongAuthoredMaterial })
            {
                if (request->generation == 0u)
                {
                    continue;
                }
                ScriptAssetRequestSnapshot snapshot{};
                Require(registry.SnapshotAssetRequest(*request, snapshot, nullptr, 0)
                    == ScriptAssetResult::Success, "Wrong-representation snapshot failed");
                if (snapshot.status == static_cast<std::int32_t>(AssetDepot::AssetRequestStatus::Pending))
                {
                    complete = false;
                    continue;
                }
                Require(snapshot.status == static_cast<std::int32_t>(AssetDepot::AssetRequestStatus::Failed)
                    && snapshot.error == static_cast<std::int32_t>(AssetDepot::AssetRequestError::UnsupportedRepresentation),
                    "Same-kind representation silently converted to a different native view");
                ScriptAssetToken rejected{};
                Require(registry.AcquireAssetResult(*request, rejected) == ScriptAssetResult::NotResident
                    && rejected.generation == 0u, "Unsupported representation produced an owner");
                Require(registry.ReleaseAsset(*request) == ScriptAssetResult::Success,
                    "Unsupported-representation request release failed");
                *request = {};
            }
            return complete;
        }

        void VerifySessionRestart(DataSystem& dataSystem)
        {
            auto& registry = ScriptObjectRegistry::Get();
            for (const auto& item : m_cases)
            {
                Require(item.owner.generation != 0u, "Poll is unfinished");
            }
            Require(m_wrongCodeProgram.generation == 0u && m_wrongGraphProgram.generation == 0u
                && m_wrongAuthoredMaterial.generation == 0u, "Representation checks are unfinished");
            registry.EndAssetSession();
            registry.BeginAssetSession(&dataSystem);
            for (auto& item : m_cases)
            {
                ScriptAssetToken next{};
                Require(registry.RequestAssetTyped(item.link, item.concreteType, {}, false, next) == ScriptAssetResult::Success,
                    "Request after CLR restart failed");
                Require(registry.ReleaseAsset(item.owner) == ScriptAssetResult::InvalidToken, "Late finalizer token revived after restart");
                Require(registry.ReleaseAsset(item.request) == ScriptAssetResult::InvalidToken, "Old request revived after restart");
                Require(registry.ReleaseAsset(next) == ScriptAssetResult::Success, "New session request release failed");
            }
            registry.EndAssetSession();
            m_started = false;
        }

    private:
        template<class T>
        static constexpr Case TypedCase(bool codeRepresentation = false)
        {
            Case result{};
            result.kind = static_cast<std::uint32_t>(AssetDepot::AssetLink<T>::kKind);
            result.concreteType = kScriptAssetConcreteType<T>;
            result.codeRepresentation = codeRepresentation;
            return result;
        }
        std::array<Case, 11> m_cases{
            TypedCase<Texture>(), TypedCase<assets::ModelAnimationDescriptor>(),
            TypedCase<assets::ModelMeshDescriptor>(), TypedCase<assets::ModelSkeletonPayload>(),
            TypedCase<assets::ModelAnimationPayload>(), TypedCase<ShaderMeta>(),
            TypedCase<material_graph::Generation>(), TypedCase<::Material>(),
            TypedCase<LX::Runtime::ShaderGeneration>(true), TypedCase<experiment::Material>(true),
            TypedCase<::Material>(true) };
        ScriptAssetToken m_wrongCodeProgram{};
        ScriptAssetToken m_wrongGraphProgram{};
        ScriptAssetToken m_wrongAuthoredMaterial{};
        bool m_started{};
    };
}
