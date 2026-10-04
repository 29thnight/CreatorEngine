#pragma once

#include "MaterialGraphProduct.h"
#include "MaterialPropertyValue.h"
#include "LXMaterialRuntime.h"
#include "Experiment/Cooked/CookedAssetManifest.h"

#include <functional>
#include <map>
#include <mutex>
#include <optional>

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
    experiment::cooked::Sha256Digest contentDigest{};
    // Explicit active-surface routing is carried by verified graph source, including cooked products.
    std::optional<std::string> surfaceBlendMode;
};

using GenerationLoader = std::function<bool(CookedProgram&, std::string&)>;

// Validate and own a candidate without changing any accepted graph or material.
std::shared_ptr<const Generation> PrepareGeneration(const experiment::AssetId& id, const GenerationLoader& loader,
                                                    std::string& error);

// A failed reload does not return success or replace the accepted generation.
// Existing instances retain their own immutable owner after reload or removal.
class GenerationStore
{
  public:
    std::shared_ptr<const Generation> Load(const experiment::AssetId& id, const GenerationLoader& loader, bool reload,
                                           std::string& error);
    bool Publish(std::shared_ptr<const Generation> candidate, const std::shared_ptr<const Generation>& expected,
                 std::string& error);
    std::shared_ptr<const Generation> Current(const experiment::AssetId& id) const;
    void Remove(const experiment::AssetId& id);
    void Clear();

  private:
    struct Entry
    {
        std::shared_ptr<const Generation> owner;
        experiment::cooked::Sha256Digest digest{};
    };
    mutable std::mutex mutex_;
    std::map<experiment::AssetId, Entry> entries_;
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
