#pragma once

#include "MaterialGraphProduct.h"
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

// A failed reload does not return success or replace the accepted generation.
// Existing instances retain their own immutable owner after reload or removal.
class GenerationStore
{
  public:
    std::shared_ptr<const Generation> Load(const experiment::AssetId& id, const GenerationLoader& loader, bool reload,
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

struct Instance
{
    std::shared_ptr<const Generation> generation;
    InstanceDescription description;
    std::vector<std::uint8_t> uniforms;
    std::vector<InstanceTexture> textures;
};

using TextureLoader =
    std::function<std::shared_ptr<Texture>(const experiment::AssetId&, LX::LXColorSpace, std::string&)>;
bool BuildInstance(std::shared_ptr<const Generation> generation, const InstanceDescription& description,
                   const TextureLoader& loadTexture, std::shared_ptr<const Instance>& result, std::string& error);

// Disk values contain stable parameter IDs and typed values, never byte offsets
// or runtime generation numbers. ShaderMeta material documents stay separate.
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
