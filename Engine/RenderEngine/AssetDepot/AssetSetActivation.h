#pragma once

#include "../Experiment/Cooked/CookedAssetCatalog.h"

namespace AssetDepot
{
    // Installed decoder compatibility only. No source, artifact or GPU work.
    // Generic CookedAssetCatalog candidate builders remain format-neutral.
    [[nodiscard]] bool ValidateAssetSetRuntimeCompatibility(
        const experiment::cooked::AssetSetManifest& manifest,
        std::vector<experiment::cooked::AssetManifestIssue>& issues);

    // Bounded host-authored activation policy. These are explicit storage I/O
    // operations for startup/package validation, never TryAcquire or frame work.
    // An absent policy is successful with no inputs. Failures preserve outInputs.
    [[nodiscard]] bool ReadConfiguredAssetSets(const std::filesystem::path& assetRoot,
        std::vector<experiment::cooked::AssetSetMountInput>& outInputs, std::string& failure);
    [[nodiscard]] bool ValidateConfiguredAssetSets(const std::filesystem::path& assetRoot,
        std::string& failure);
}
