#pragma once
#include "../../Engine/RenderEngine/MaterialGraphRuntime.h"

void VerifyMaterialRuntime(const material_graph::VerifiedProduct& numericProduct,
                           const experiment::cooked::CookedAssetCatalog& catalog,
                           const experiment::cooked::ArtifactByteSource& loose,
                           const experiment::cooked::ArtifactByteSource& mounted, const LX::LXMaterialAsset& source,
                           const experiment::AssetId& graphId);
