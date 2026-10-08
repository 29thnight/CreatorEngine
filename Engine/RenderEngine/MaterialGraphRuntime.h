#pragma once

#include "MaterialGraphProduct.h"
#include "Ownership.h"
#include "MaterialPropertyValue.h"
#include "LXMaterialRuntime.h"
#include "Experiment/Cooked/CookedAssetManifest.h"

#include <functional>
#include <map>
#include <mutex>

class Texture;
namespace AssetDepot
{
    struct MaterialProgramAssetOrigin;
}
namespace Authoring
{
    class ReadNode;
    class WriteNode;
} // namespace Authoring
namespace experiment::cooked
{
    class ArtifactByteSource;
    class CookedAssetCatalog;
} // namespace experiment::cooked

namespace material_graph
{
    struct Generation
    {
        experiment::AssetId assetId;
        std::uint64_t generation{};
        CookedProgram cooked;
        // AssetDepot pins the exact artifact and resolved default Texture
        // representations. Legacy source generations leave this empty.
        own::shared_owner<const AssetDepot::MaterialProgramAssetOrigin> assetOrigin{};

        // Conservative CPU capacity charge, including the reachable generated
        // shader and exact AssetDepot origin/default Texture descriptor closure.
        // This excludes allocator/control-block overhead and is not reclaimed bytes.
        std::size_t RetainedPayloadBytes() const noexcept;
    };

    using GenerationLoader = std::function<bool(CookedProgram&, std::string&)>;

    struct GenerationPreparationState;

    // A copyable ticket contains no reference to the store itself. Workers may
    // finish preparation after invalidation; publication still rejects the ticket.
    class GenerationPreparationRequest
    {
    public:
        GenerationPreparationRequest() = default;
        explicit operator bool() const noexcept { return bool(m_state) || bool(m_cachedOwner); }

    private:
        friend class GenerationStore;
        own::shared_owner<GenerationPreparationState> m_state;
        own::shared_owner<const Generation> m_cachedOwner;
        experiment::cooked::Sha256Digest m_cachedDigest{};
        std::uint64_t m_requestGeneration{};
    };

    // Prepared payloads are immutable, including their reserved generation ID.
    // Only Publish makes one visible through Current or any runtime instance.
    class PreparedGeneration
    {
    public:
        // Public construction is restricted to GenerationStore without accessing
        // ownership_cpp internals or adopting a separately allocated pointer.
        class ConstructionKey
        {
            friend class GenerationStore;
            ConstructionKey() = default;
        };
        PreparedGeneration(ConstructionKey, own::shared_owner<GenerationPreparationState> state,
                           own::shared_owner<const Generation> owner,
                           experiment::cooked::Sha256Digest digest, std::uint64_t requestGeneration);

    private:
        friend class GenerationStore;
        const own::shared_owner<GenerationPreparationState> m_state;
        const own::shared_owner<const Generation> m_owner;
        const experiment::cooked::Sha256Digest m_digest;
        const std::uint64_t m_requestGeneration;
    };

    // A failed reload does not return success or replace the accepted generation.
    // Existing instances retain their own immutable owner after reload or removal.
    class GenerationStore
    {
    private:
        struct Entry
        {
            own::weak_owner<const Generation> current;
            own::shared_owner<const Generation> retained;
            experiment::cooked::Sha256Digest digest{};
            own::weak_owner<GenerationPreparationState> request;
            std::uint64_t currentGeneration{}, requestGeneration{};
            mutable std::uint64_t lastUse{};
            std::size_t retainedBytes{};
            bool dirty{};
        };

    public:
        // Construct before taking outer admission locks, and destroy after those
        // locks are released. Empty map construction may allocate on some STLs.
        class RetiredEntries final
        {
        public:
            RetiredEntries() = default;
            RetiredEntries(const RetiredEntries&) = delete;
            RetiredEntries& operator=(const RetiredEntries&) = delete;
            RetiredEntries(RetiredEntries&&) = delete;
            RetiredEntries& operator=(RetiredEntries&&) = delete;
            ~RetiredEntries() = default;

        private:
            friend class GenerationStore;
            std::map<experiment::AssetId, Entry> entries_;
        };

        // Non-reload requests share one preparation for the current revision.
        // Reloads supersede its ticket without removing the accepted owner;
        // dirty entries must prepare successfully before they are cache hits.
        GenerationPreparationRequest BeginPreparation(const experiment::AssetId& id, bool reload,
                                                      std::string& error);
        // Loader, verification, serialization and hashing never hold the store
        // mutex. The same cold ticket runs its loader only once.
        static own::shared_owner<const PreparedGeneration> Prepare(const GenerationPreparationRequest& request,
                                                                 const GenerationLoader& loader, std::string& error);
        // Call on the owning publication boundary. Removal, Clear or a newer
        // reload invalidates old tickets; identical payloads retain their owner.
        own::shared_owner<const Generation> Publish(const PreparedGeneration& prepared, std::string& error);
        own::shared_owner<const Generation> Load(const experiment::AssetId& id, const GenerationLoader& loader, bool reload,
                                              std::string& error);
        own::shared_owner<const Generation> Current(const experiment::AssetId& id) const;
        // Preserve Current for existing consumers, but reject outstanding tickets
        // and make subsequent loads prepare/join the changed source revision.
        void InvalidatePreparation(const experiment::AssetId& id);
        void Remove(const experiment::AssetId& id);
        // Requires empty caller-owned storage; detaches without allocating or
        // destroying retained owners under this cache or outer admission locks.
        void DetachAll(RetiredEntries& retired) noexcept;
        void Clear();
        // Eviction only releases this cache's references; consumers and requests
        // keep their exact generations. Zero disables cache retention.
        void SetRetainedBudgetBytes(std::size_t bytes);
        std::size_t RetainedBytes() const;
        std::size_t RetainedBudgetBytes() const;
        // Reserve in the same never-reset identity space as legacy preparation.
        // Zero means exhausted. Caller may already hold asset admission.
        [[nodiscard]] std::uint64_t ReserveGeneration();

    private:
        void RetainLocked(Entry& entry, const own::shared_owner<const Generation>& owner,
                          std::vector<own::shared_owner<const Generation>>& released);
        void TrimRetainedLocked(std::vector<own::shared_owner<const Generation>>& released);
        std::uint64_t NextUseLocked() const noexcept;
        mutable std::mutex mutex_;
        std::map<experiment::AssetId, Entry> entries_;
        std::size_t retainedBudgetBytes_ = 128u * 1024u * 1024u;
        std::size_t retainedBytes_{};
        mutable std::uint64_t useSerial_{};
        // Reserved at BeginPreparation, never reset by Remove/Clear. Failed or
        // unchanged preparations may leave gaps, but an ID is never reused.
        std::uint64_t serial_{};
    };

    // An authoring host supplies the current source graph for exact freshness.
    // Packaged loads need neither a source graph nor the Slang compiler.
    bool LoadCookedGeneration(const experiment::cooked::CookedAssetCatalog& catalog,
                              const experiment::cooked::ArtifactByteSource& bytes, const experiment::AssetId& id,
                              const LX::LXMaterialAsset* source, CookedProgram& result, std::string& error);

    struct TextureOverride
    {
        LX::Id parameter{};
        experiment::AssetId assetId;
    };

    struct InstanceDescription
    {
        experiment::AssetId graphId;
        std::vector<ParameterOverride> parameters;
        std::vector<TextureOverride> textures;
    };

    struct InstanceTexture
    {
        std::uint32_t slot{};
        experiment::AssetId assetId;
        LX::LXColorSpace colorSpace{};
        // Texture CPU owners migrate in their own slice; retain this exact owner.
        own::shared_owner<const Texture> owner;
    };

    struct Instance : LX::Runtime::Instance
    {
        own::shared_owner<const Generation> generation;
        InstanceDescription description;
        std::vector<InstanceTexture> textures;
    };

    using TextureLoader =
        std::function<own::shared_owner<const Texture>(const experiment::AssetId&, LX::LXColorSpace, std::string&)>;
    bool BuildInstance(own::shared_owner<const Generation> generation, const InstanceDescription& description,
                       const TextureLoader& loadTexture, own::shared_owner<const Instance>& result, std::string& error);

    // Disk values contain stable parameter IDs and typed values, never byte offsets
    // or runtime generation numbers. Generated ShaderMeta is derived from the graph.
    struct InstanceDocument
    {
        std::string name;
        experiment::AssetId materialId;
        bool doubleSided{};
        InstanceDescription description;
        std::string blendMode = "opaque";
    };

    bool WriteInstanceDocument(const InstanceDocument& document, Authoring::WriteNode result, std::string& error);
    bool ReadInstanceDocument(const Authoring::ReadNode& node, InstanceDocument& result, std::string& error);
} // namespace material_graph
