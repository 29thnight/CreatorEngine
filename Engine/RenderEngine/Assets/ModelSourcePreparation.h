#include "../../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "ModelSidecarV2.h"

namespace assets
{
    struct ModelGeometryImportSettings final
    {
        bool buildMeshlets{true}; // Mandatory LX cooking policy, not a route switch.
        std::uint32_t lodLevels{};
        bool createMeshCollider{};
    };

    // 캡처한 sidecar만 읽어 저작과 cook의 기본값·설정 해석이 갈라지지 않게 한다.
    [[nodiscard]] bool ReadModelGeometryImportSettings(std::string_view sidecarText,
        ModelGeometryImportSettings& out, std::string& failure);

    // Read-only common import preparation. Identity issuance/publication remains
    // exclusively in ModelAssetAuthoringTransaction.
    [[nodiscard]] own::unique_owner<experiment::importer::IAssetImporter>
        CreateModelSourceImporter(const std::filesystem::path& source);
    [[nodiscard]] bool NormalizeModelStableInputs(ce::dynamic_array<StableKeyElement>& elements,
        std::string& failure);
    [[nodiscard]] bool ReconcileAuthoredModelBindings(const experiment::importer::ImportedScene& scene,
        const ModelSidecarV2& sidecar, ce::dynamic_array<StableKeyAssignment>& out,
        std::string& failure);
}
