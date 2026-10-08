#pragma once

#include "ModelSidecarV2.h"
#include "../../Utility_Framework/Ownership.h"

namespace assets
{
    struct ModelGeometryImportSettings final
    {
        bool buildMeshlets{};
        std::uint32_t lodLevels{};
    };

    // 캡처한 sidecar만 읽어 저작과 cook의 기본값·설정 해석이 갈라지지 않게 한다.
    [[nodiscard]] bool ReadModelGeometryImportSettings(std::string_view sidecarText,
        ModelGeometryImportSettings& out, std::string& failure);

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
