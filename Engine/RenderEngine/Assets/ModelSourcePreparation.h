#pragma once

#include "ModelSidecarV2.h"
#include "../../Utility_Framework/Ownership.h"

namespace assets
{
    // Read-only common import preparation. Identity issuance/publication remains
    // exclusively in ModelAssetAuthoringTransaction.
    [[nodiscard]] own::unique_owner<experiment::importer::IAssetImporter>
        CreateModelSourceImporter(const std::filesystem::path& source);
    [[nodiscard]] bool NormalizeModelStableInputs(std::vector<StableKeyElement>& elements,
        std::string& failure);
    [[nodiscard]] bool ReconcileAuthoredModelBindings(const experiment::importer::ImportedScene& scene,
        const ModelSidecarV2& sidecar, std::vector<StableKeyAssignment>& out,
        std::string& failure);
}
