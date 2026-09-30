#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
// Standalone material files become owned CEDO bytes and manifest edges.
// Legacy materials reference ShaderMeta and texture properties. Lattice
// documents reference their graph and texture overrides; their typed schema
// and sidecar identity are validated before publication. Numeric overrides
// are checked against the compiled graph by AssetCooker.
struct MaterialCookProductRequest final
{
    std::filesystem::path sourcePath{};
    std::filesystem::path assetRoot{};
};

struct MaterialCookProduct final
{
    AssetId materialAssetId{};
    AssetId shaderMetaAssetId{};
    std::string artifactPath{};
    std::vector<std::byte> artifactBytes{};
    CookedAssetManifestEntry manifestEntry{};

    // 진단용. identity 가 아니다.
    std::string name{};
    std::size_t texturePropertyCount{};
    std::size_t distinctTextureCount{};
};

struct MaterialCookProductIssue final
{
    std::string context{};
    std::string message{};
};

struct MaterialCookProductResult final
{
    std::optional<MaterialCookProduct> product{};
    std::vector<MaterialCookProductIssue> issues{};

    [[nodiscard]] bool Succeeded() const noexcept { return product.has_value() && issues.empty(); }
};

// 확장자 불일치, `.meta` 누락·비정규 GUID, YAML 파싱 실패,
// `m_shaderMetaGuid` 누락·비정규, texture GUID 비정규, source-root 탈출,
// 빈 파일은 모두 게시 전에 실패한다.
[[nodiscard]] MaterialCookProductResult BuildMaterialCookProduct(const MaterialCookProductRequest& request);
} // namespace experiment::cooked
