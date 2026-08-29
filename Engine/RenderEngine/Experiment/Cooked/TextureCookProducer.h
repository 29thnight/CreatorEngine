#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    // Source texture 하나를 publication 직전의 완전 소유 산출물로 바꾼다.
    // `ModelCookProducer` 와 같은 규약이다 — 파일은 쓰지 않고, AssetCooker 가
    // 여러 product 를 한 staging 디렉터리에 모아 원자적으로 게시한다.
    //
    // ★ **이 슬라이스의 artifact 는 원본 바이트 그대로다. 트랜스코딩하지 않는다.**
    //
    //   BC7 압축·밉 생성은 여기 없다. 그건 압축기를 들이는 별개의 작업이고,
    //   지금 넣으면 D5-b2c 가 그 작업에 묶인다. `BuildPipelinePlan` B3 가
    //   셰이더에 대해 이미 같은 자세를 취한다 — artifact 경로가 서기 전까지
    //   HLSL source 를 pak 에 싣는다.
    //
    //   그러면 무엇을 얻는가. **압축이 아니라 주소 체계다:**
    //     - GUID 주소  — `.meta` 를 읽어 경로를 찾는 런타임 탐색이 사라진다
    //     - 내용 해시  — stale artifact 를 fail-closed 로 잡는다
    //     - manifest 등재 — 의존 폐포(D5-b2c-5)의 노드가 된다
    //
    //   이것이 D5-c(Player 소비)가 실제로 필요로 하는 것이고, 트랜스코딩은
    //   그 위에 나중에 얹으면 된다. `formatVersion` 이 1 이므로 트랜스코딩이
    //   들어오는 날 2 가 되고 구버전 artifact 는 자동으로 거부된다.
    //
    // ★ 확장자는 artifactPath 에 남긴다. 로더가 그것으로 디코더를 고른다.
    //   지금은 pass-through 라 확장자가 곧 포맷이다.

    struct TextureCookProductRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
    };

    struct TextureCookProduct final
    {
        AssetId textureAssetId{};
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        CookedAssetManifestEntry manifestEntry{};

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
    // 여기 있는 셋은 현재 corpus 가 실제로 가진 전부이고(png 99·hdr 19·dds 1)
    // 게이트가 셋 다 태운다. 넷째를 더하려면 게이트 사례도 함께 더해야 한다 —
    // 목록만 늘리면 검증되지 않은 경로가 열린다.
    [[nodiscard]] bool IsSupportedTextureExtension(
        std::string_view lowercaseExtension) noexcept;

    // texture `.meta` 의 UUIDv4 만 identity 로 쓴다. source-root 탈출, `.meta`
    // 누락, 비정규 GUID, 지원하지 않는 확장자, 빈 파일은 게시 전에 실패한다.
    [[nodiscard]] TextureCookProductResult BuildTextureCookProduct(
        const TextureCookProductRequest& request);
}
