#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    // Offline source image -> validated GPU-ready CECT artifact. The producer
    // owns every returned byte and never publishes files. Source and metadata
    // are captured together; callers can supply their build-local pinned reader.
    struct TextureCookProductRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
        // A failed supplied reader never falls back to a different disk read.
        std::function<bool(const std::filesystem::path&, std::vector<std::byte>&,
            std::string&)> captureSource{};
    };

    struct TextureCookProduct final
    {
        AssetId textureAssetId{};
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        CookedAssetManifestEntry manifestEntry{};
        Sha256Digest sourceContentSha256{};
        Sha256Digest sourceMetaSha256{};
        std::string importRecipe{};

        // 진단용이다. identity 가 아니다 — 확장자로 GUID 를 만들지 않는다.
        std::string sourceExtension{};
    };

    struct TextureCookProductIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct TextureCookProductResult final
    {
        std::optional<TextureCookProduct> product{};
        std::vector<TextureCookProductIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    // 확장자 allowlist 다. **모르는 확장자는 fail-closed** — 조용히 통과시키면
    // 런타임이 디코드할 수 없는 artifact 가 pak 에 실린다.
    //
    // 디스크 corpus 는 png 99·hdr 19·dds 1 이고, 임베디드 texture 가 여기에
    // `.jpg` 를 더한다. 게이트가 네 가지를 모두 태운다 — 다섯째를 더하려면
    // 게이트 사례도 함께 더해야 한다. 목록만 늘리면 검증되지 않은 경로가 열린다.
    [[nodiscard]] bool IsSupportedTextureExtension(
        std::string_view lowercaseExtension) noexcept;

    // 임베디드 texture 바이트에서 컨테이너를 판별한다. 모르면 빈 문자열이다.
    //
    // ★ `ImportedTexture::mimeType` 을 쓰지 않는다. **그 필드는 아무도 채우지
    //   않는다** — glTF 임포터가 `sourceKey`·`name`·`colorSpace`·`embeddedBytes`
    //   만 설정하고 `mimeType` 은 건드리지 않는다(ufbx 경로도 마찬가지).
    //   비어 있는 필드를 신뢰하면 임베디드 텍스처가 전부 "확장자 불명"이 된다.
    //
    //   매직 바이트는 컨테이너 자신이 들고 있는 사실이라 sidecar 가 틀려도
    //   맞는다. 그래서 여기서만 판별하고, 결과는 위의 allowlist 를 다시 통과해야
    //   한다 — 판별과 허용을 한 곳에서 하면 sniffer 가 늘어날 때 조용히 허용
    //   범위가 넓어진다.
    [[nodiscard]] std::string_view SniffTextureExtension(
        std::span<const std::byte> bytes) noexcept;

    // texture `.meta` 의 UUIDv4 만 identity 로 쓴다. source-root 탈출, `.meta`
    // 누락, 비정규 GUID, 지원하지 않는 확장자, 빈 파일은 게시 전에 실패한다.
    [[nodiscard]] TextureCookProductResult BuildTextureCookProduct(
        const TextureCookProductRequest& request);
}
