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

    // 모델 컨테이너 안에 들어 있던 texture 를 Derived artifact 로 뽑은 것.
    //
    // ★ 이게 없으면 재질의 texture 의존이 **해소 불가능한 GUID** 가 된다.
    //   실측상 texture 참조 100 개 중 96 개가 임베디드다 — 즉 지배적인 경우다.
    //   CEMC 는 texture 바이트를 싣지 않으므로(TextureReference 는 ID 와 진단
    //   경로만 든다), 뽑지 않으면 그 GUID 를 가리키는 artifact 가 어디에도 없다.
    struct EmbeddedTextureArtifact final
    {
        AssetId textureAssetId{};
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        std::string sourceKey{};      // 진단용. identity 가 아니다.
        std::string extension{};      // 매직 바이트로 판별한 결과
    };

    struct ModelCookProduct final
    {
        AssetId modelAssetId{};
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        std::vector<CookedAssetManifestEntry> manifestEntries{};
        std::vector<EmbeddedTextureArtifact> embeddedTextures{};
        std::size_t materialCount{};
        std::size_t embeddedTextureCount{};
        std::size_t textureReferenceCount{};
        std::size_t externalTextureReferenceCount{};
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
