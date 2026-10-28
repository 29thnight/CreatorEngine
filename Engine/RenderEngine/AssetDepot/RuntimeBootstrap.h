#include "../../Containers/ContainerAPI.h"
#pragma once

#include "../Experiment/Cooked/CookedAssetCatalog.h"

namespace AssetDepot
{
    struct RuntimeBootstrapDocument final
    {
        experiment::AssetId assetId{};
        experiment::cooked::Sha256Digest contentSha256{};
        ce::dynamic_array<experiment::cooked::TypedAssetReference> references{};
    };

    // Explicit document bridge. CEMF v2 keeps its unchanged local edge meaning.
    struct RuntimeBootstrapReceipt final
    {
        experiment::cooked::Sha256Digest legacyManifestSha256{};
        ce::dynamic_array<std::string> assetSetHashes{};
        ce::dynamic_array<RuntimeBootstrapDocument> documents{};
    };

    [[nodiscard]] bool WriteRuntimeBootstrapReceipt(const RuntimeBootstrapReceipt& receipt,
        std::string& text, std::string& failure);
    [[nodiscard]] bool ReadRuntimeBootstrapReceipt(std::string_view text,
        RuntimeBootstrapReceipt& receipt, std::string& failure);
    // Reads metadata only. Absence selects the legacy package contract.
    [[nodiscard]] bool ValidateRuntimeBootstrap(const std::filesystem::path& assetRoot,
        const ce::dynamic_array<std::string>& assetSetHashes,
        const experiment::cooked::CookedAssetCatalog& candidate, std::string& failure);
}
