#pragma once

#include "CookedAssetManifest.h"
#include "../../MaterialGraphProduct.h"

namespace experiment::cooked
{
class ArtifactByteSource;

struct MaterialProgramCookProduct
{
    CookedAssetManifestEntry manifestEntry;
    std::vector<std::byte> artifactBytes;
};

// The source graph is regenerated with the canonical compiler before cook.
// A verified host specialization for another graph is never published.
bool BuildMaterialProgramCookProduct(const AssetId& graphId, const LX::LXMaterialAsset& graph,
                                     const material_graph::VerifiedProduct& verified,
                                     const material_graph::Budget& budget, MaterialProgramCookProduct& result,
                                     std::string& error);

// Mounted/loose bytes are copied and SHA-256 checked before decoding. The
// returned generation owns its bytecode and needs no runtime Slang compiler.
bool OpenCookedMaterialProgram(const CookedAssetManifestEntry& entry, const ArtifactByteSource& bytes,
                               const material_graph::Budget& budget, material_graph::CookedProgram& result,
                               std::string& error);
} // namespace experiment::cooked
