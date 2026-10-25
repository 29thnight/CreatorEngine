#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Uuid.h"
#else
import Creator.Core.Uuid;
#endif

#include "ModelAssetGeneration.h"
#include "../Experiment/ModelData.h"
#include "../../../Lattice/Material/LXMaterialGraph.h"

namespace assets
{
// Imported PBR data seeds the graph once. Subsequent imports retain authored
// graphs; model/mesh/material/texture identities and CEMC bytes stay unchanged.
uuid::uuid16 ModelMaterialGraphId(const uuid::uuid16& materialId);
std::filesystem::path ModelMaterialGraphPath(const std::filesystem::path& assets, const uuid::uuid16& modelId,
                                             const uuid::uuid16& materialId);
bool ModelMaterialGraphsPresent(const std::filesystem::path& assets, const ModelAssetGeneration& model);
bool ModelMaterialGraphIdentityMatches(const std::filesystem::path& path, const uuid::uuid16& graphId);
std::optional<LX::LXMaterialAsset> BuildModelMaterialGraph(const experiment::Material& material, std::string& error);
    std::optional<LX::LXMaterialAsset> BuildDefaultMaterialGraph(std::string& error);

// New files are staged, then published before the model commit record. Failure
// rolls back only files owned by this publication; existing graphs are read-only.
class ModelMaterialGraphPublication
{
  public:
    ~ModelMaterialGraphPublication();
    bool Prepare(const std::filesystem::path& assets, const uuid::uuid16& modelId,
                 std::span<const experiment::Material> materials, std::string& error);
    bool Publish(std::string& error);
    void Commit();

  private:
    struct File
    {
        std::filesystem::path staging, destination;
        bool published{};
    };
    std::vector<File> files_;
    bool committed_{};
};
} // namespace assets
