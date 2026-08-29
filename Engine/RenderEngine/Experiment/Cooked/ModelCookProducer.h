#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // Source model 하나를 publication 직전의 완전 소유 산출물로 바꾼다.
    // 파일 게시는 하지 않는다. AssetCooker가 여러 product를 한 staging
    // 디렉터리에 모은 뒤 manifest와 함께 원자적으로 게시한다.
    struct ModelCookProductRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
    };

    struct ModelCookProduct final
    {
        AssetId modelAssetId{};
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        std::vector<CookedAssetManifestEntry> manifestEntries{};
        std::size_t materialCount{};
        std::size_t embeddedTextureCount{};
        std::size_t textureReferenceCount{};
    };

    struct ModelCookProductIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct ModelCookProductResult final
    {
        std::optional<ModelCookProduct> product{};
        std::vector<ModelCookProductIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    // model .meta가 소유한 model/material/embedded-texture UUIDv4와 각 외부
    // texture/ShaderMeta sidecar UUIDv4만 사용한다. 누락·stale identity,
    // source-root 탈출, checked writer 거부는 모두 publication 전에 실패한다.
    [[nodiscard]] ModelCookProductResult BuildModelCookProduct(
        const ModelCookProductRequest& request);
}
