// Unrun source fixture for a dedicated engine test host. Not a standalone build
// target: include this source in a harness with a source-free v3 mount containing
// at least one root of every registered kind. Call Begin, tick Poll on the GT
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
    static_assert(kScriptAssetConcreteType<experiment::Material> == 0u);
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
    };

    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    class Driver final
    {
    public:
        Driver() = default;
        Driver(const Driver&) = delete;
        Driver& operator=(const Driver&) = delete;
        ~Driver() { if (m_started) ScriptObjectRegistry::Get().EndAssetSession(); }

        void Begin(DataSystem& dataSystem, std::uint64_t mount)
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
                Require(item.link.kind == item.kind && item.link.reserved == 0u, "Root kind mismatch");

                ScriptAssetToken rejected{};
                auto malformed = item.link;
                malformed.reserved = 1u;
                Require(registry.RequestAsset(malformed, {}, false, rejected) == ScriptAssetResult::InvalidLink
                    && rejected.generation == 0u, "Reserved link field accepted");
                malformed = item.link;
                malformed.kind |= 0x100u;
                Require(registry.RequestAsset(malformed, {}, false, rejected) == ScriptAssetResult::UnsupportedType,
                    "Manifest kind was truncated before dispatch");
                if (item.kind != static_cast<std::uint32_t>(Kind::Texture))
                {
                    Require(registry.RequestAsset(item.link, { 1u, 0u, 0u }, false, rejected)
                        == ScriptAssetResult::InvalidArgument, "Non-texture options ignored");
                }

                ScriptAssetToken cancelled{};
                Require(registry.RequestAsset(item.link, {}, false, cancelled) == ScriptAssetResult::Success,
                    "First consumer request failed");
                Require(registry.RequestAsset(item.link, {}, false, item.request) == ScriptAssetResult::Success,
                    "Second consumer request failed");
                Require(cancelled.index != item.request.index, "Consumers share one native slot");
                Require(item.request.type == (item.concreteType | kScriptAssetRequestBit), "Wrong concrete request type");
                Require(registry.CancelAssetRequest(cancelled) == ScriptAssetResult::Success, "Cancel failed");
                Require(registry.ReleaseAsset(cancelled) == ScriptAssetResult::Success, "Request release failed");
                Require(registry.ReleaseAsset(cancelled) == ScriptAssetResult::InvalidToken, "Duplicate release accepted");
            }
        }

        // Return to the normal GT/scheduler between calls. Failed/cancelled/stale
        // siblings are errors, not permission to call a pending request complete.
        bool Poll()
        {
            auto& registry = ScriptObjectRegistry::Get();
            bool complete = true;
            for (auto& item : m_cases)
            {
                if (item.owner.generation != 0u) continue;
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
                Require(registry.RequestAsset(item.link, {}, true, resident) == ScriptAssetResult::Success,
                    "First result owner did not preserve resident generation after request disposal");
                Require(registry.ReleaseAsset(second) == ScriptAssetResult::InvalidToken, "Reused slot accepted a stale token");
                Require(registry.ReleaseAsset(resident) == ScriptAssetResult::Success, "Resident owner release failed");
            }
            return complete;
        }

        void VerifySessionRestart(DataSystem& dataSystem)
        {
            auto& registry = ScriptObjectRegistry::Get();
            for (const auto& item : m_cases) Require(item.owner.generation != 0u, "Poll is unfinished");
            registry.EndAssetSession();
            registry.BeginAssetSession(&dataSystem);
            for (auto& item : m_cases)
            {
                ScriptAssetToken next{};
                Require(registry.RequestAsset(item.link, {}, false, next) == ScriptAssetResult::Success,
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
        static constexpr Case TypedCase()
        {
            return { static_cast<std::uint32_t>(AssetDepot::AssetLink<T>::kKind), kScriptAssetConcreteType<T> };
        }
        std::array<Case, 8> m_cases{
            TypedCase<Texture>(), TypedCase<assets::ModelAnimationDescriptor>(),
            TypedCase<assets::ModelMeshDescriptor>(), TypedCase<assets::ModelSkeletonPayload>(),
            TypedCase<assets::ModelAnimationPayload>(), TypedCase<ShaderMeta>(),
            TypedCase<material_graph::Generation>(), TypedCase<::Material>() };
        bool m_started{};
    };
}
