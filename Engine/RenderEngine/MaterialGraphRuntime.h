#pragma once

#include "MaterialGraphProduct.h"
#include "MaterialPropertyValue.h"
#include "LXMaterialRuntime.h"
#include "Experiment/Cooked/CookedAssetManifest.h"

#include <functional>
#include <map>
#include <mutex>

class Texture;
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
};

using GenerationLoader = std::function<bool(CookedProgram&, std::string&)>;

    struct GenerationPreparationState;

    // A copyable ticket contains no reference to the store itself. Workers may
    // finish preparation after invalidation; publication still rejects the ticket.
    class GenerationPreparationRequest
    {
    public:
        GenerationPreparationRequest() = default;
        explicit operator bool() const noexcept { return m_state != nullptr; }

    private:
        friend class GenerationStore;
        std::shared_ptr<GenerationPreparationState> m_state;
        std::shared_ptr<const Generation> m_cachedOwner;
        experiment::cooked::Sha256Digest m_cachedDigest{};
    };

    // Prepared payloads are immutable, including their reserved generation ID.
    // Only Publish makes one visible through Current or any runtime instance.
    class PreparedGeneration
    {
    private:
        friend class GenerationStore;
        PreparedGeneration(std::shared_ptr<GenerationPreparationState> state,
                           std::shared_ptr<const Generation> owner,
                           experiment::cooked::Sha256Digest digest, bool cached);

        const std::shared_ptr<GenerationPreparationState> m_state;
        const std::shared_ptr<const Generation> m_owner;
        const experiment::cooked::Sha256Digest m_digest;
        const bool m_cached;
    };

    // A failed reload does not return success or replace the accepted generation.
    // Existing instances retain their own immutable owner after reload or removal.
    class GenerationStore
    {
    public:
        // Non-reload requests share one preparation for the current revision.
        // Reloads supersede its ticket without removing the accepted owner;
        // dirty entries must prepare successfully before they are cache hits.
        GenerationPreparationRequest BeginPreparation(const experiment::AssetId& id, bool reload,
                                                      std::string& error);
        // Loader, verification, serialization and hashing never hold the store
        // mutex. The same cold ticket runs its loader only once.
        static std::shared_ptr<const PreparedGeneration> Prepare(const GenerationPreparationRequest& request,
                                                                 const GenerationLoader& loader, std::string& error);
        // Call on the owning publication boundary. Removal, Clear or a newer
        // reload invalidates old tickets; identical payloads retain their owner.
        std::shared_ptr<const Generation> Publish(const PreparedGeneration& prepared, std::string& error);
        std::shared_ptr<const Generation> Load(const experiment::AssetId& id, const GenerationLoader& loader, bool reload,
                                              std::string& error);
        std::shared_ptr<const Generation> Current(const experiment::AssetId& id) const;
        // Preserve Current for existing consumers, but reject outstanding tickets
        // and make subsequent loads prepare/join the changed source revision.
        void InvalidatePreparation(const experiment::AssetId& id);
        void Remove(const experiment::AssetId& id);
        void Clear();

    private:
        struct Entry
        {
            std::shared_ptr<const Generation> owner;
            experiment::cooked::Sha256Digest digest{};
            std::shared_ptr<GenerationPreparationState> request;
            bool dirty{};
        };
        mutable std::mutex mutex_;
        std::map<experiment::AssetId, Entry> entries_;
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
    std::shared_ptr<Texture> owner;
};

struct Instance : LX::Runtime::Instance
{
    std::shared_ptr<const Generation> generation;
    InstanceDescription description;
    std::vector<InstanceTexture> textures;
};

using TextureLoader =
    std::function<std::shared_ptr<Texture>(const experiment::AssetId&, LX::LXColorSpace, std::string&)>;
bool BuildInstance(std::shared_ptr<const Generation> generation, const InstanceDescription& description,
                   const TextureLoader& loadTexture, std::shared_ptr<const Instance>& result, std::string& error);

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
