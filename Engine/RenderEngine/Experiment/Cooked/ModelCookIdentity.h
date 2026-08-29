#pragma once

#include "../AssetIdentity.h"
#include "../Import/ImportedScene.h"

#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    enum class ModelIdentityIssueCode
    {
        InvalidDocument,
        MissingField,
        InvalidAssetId,
        DuplicateSourceKey,
        DuplicateAssetId,
        SourceMismatch,
    };

    struct ModelIdentityIssue final
    {
        ModelIdentityIssueCode code{ ModelIdentityIssueCode::InvalidDocument };
        std::string context{};
        std::string message{};
    };

    struct ModelSubAssetIdentity final
    {
        std::string sourceKey{};
        std::string name{}; // 진단/재import UI용이며 identity 판정에는 쓰지 않는다.
        AssetId assetId{};
    };

    struct ModelCookIdentity final
    {
        AssetId modelAssetId{};
        std::vector<ModelSubAssetIdentity> materials{};
        std::vector<ModelSubAssetIdentity> embeddedTextures{};

        [[nodiscard]] AssetId FindMaterial(std::string_view sourceKey) const noexcept;
        [[nodiscard]] AssetId FindEmbeddedTexture(std::string_view sourceKey) const noexcept;
    };

    // 임의 asset sidecar의 최상위 guid만 읽는다. canonical UUIDv4 외의 표기는
    // legacy 호환 없이 거부한다.
    [[nodiscard]] bool ReadAssetIdFromMeta(std::string_view yaml,
        AssetId& outAssetId, std::vector<ModelIdentityIssue>& outIssues);

    // model sidecar schema:
    // subAssets.schemaVersion: 1
    // subAssets.materials[] / embeddedTextures[]: { key, name?, guid }
    [[nodiscard]] bool ReadModelCookIdentity(std::string_view yaml,
        ModelCookIdentity& outIdentity,
        std::vector<ModelIdentityIssue>& outIssues);

    // sidecar가 현재 import 결과와 정확히 일치하는지 검사한다. 누락뿐 아니라
    // source에서 사라진 stale subasset도 게시 전에 막는다.
    [[nodiscard]] bool ValidateModelCookIdentity(
        const importer::ImportedScene& scene,
        const ModelCookIdentity& identity,
        std::vector<ModelIdentityIssue>& outIssues);
}
