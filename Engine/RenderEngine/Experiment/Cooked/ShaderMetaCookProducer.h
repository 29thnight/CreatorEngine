#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // `.shadermeta` 하나를 publication 직전의 완전 소유 산출물로 바꾼다.
    // `ModelCookProducer`·`TextureCookProducer` 와 같은 규약이다.
    //
    // ★ artifact 는 D6 CEDO tree bytes다. `formatVersion`은 ShaderMeta schema와
    //   CEDO version을 함께 담으므로 어느 한쪽이 바뀌어도 재쿠킹된다.
    //
    // ★ **검증은 `ShaderMetaLoader::Parse` 가 한다. 여기서 두 번째 파서를 만들지
    //   않는다.** 파서가 갈라지면 어떤 `.shadermeta` 는 에디터에서 통과하고
    //   cook 에서 거부되거나 그 반대가 된다 — 그 순간 둘 중 어느 쪽이 정본인지
    //   말할 수 없게 된다.
    //
    // ★ **HLSL source 는 manifest dependency 로 넣지 않는다.**
    //
    //   `source:` 가 가리키는 `.hlsl` 은 GUID 와 `.meta` 를 가지고 있지만
    //   Derived artifact 가 아니다 — B2 가 그것을 **content** 로 pak 에 싣고
    //   경로로 주소를 매긴다(`build.ps1` 이 "B3 전까지 shader 는 source HLSL 을
    //   pak 에 포함한다"고 직접 적는다). 여기서 `Derived/Shaders/...` 를 만들면
    //   셰이더 artifact 의 소유자가 둘이 되고, 이 저장소가 반복해서 밟은 함정이
    //   정확히 그것이다("경로를 만드는 지점이 갈라지면 반드시 어긋난다").
    //
    //   대신 **해소 가능한지는 여기서 증명한다** — source 가 asset root 안에
    //   실재하고 canonical `.meta` GUID 를 가져야 통과한다. 즉 간선을 조용히
    //   버리는 것이 아니라, 간선을 그릴 주체가 B3 라는 것이다.

    struct ShaderMetaCookProductRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
    };

    struct ShaderMetaCookProduct final
    {
        AssetId shaderMetaAssetId{};

        // 검증된 HLSL 의 GUID. manifest 에는 들어가지 않지만 도구가 요약에
        // 찍는다 — 아무도 안 읽는 필드로 두지 않는다.
        AssetId sourceShaderAssetId{};

        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        CookedAssetManifestEntry manifestEntry{};

        // 진단용. identity 가 아니다.
        std::string name{};
        std::string sourceRelativePath{};
        std::size_t propertyCount{};
        std::size_t keywordAxisCount{};
        std::size_t passCount{};
    };

    struct ShaderMetaCookProductIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct ShaderMetaCookProductResult final
    {
        std::optional<ShaderMetaCookProduct> product{};
        std::vector<ShaderMetaCookProductIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    // 확장자 불일치, `.meta` 누락·비정규 GUID, schema 위반, source 누락,
    // source-root 탈출, 빈 파일은 모두 게시 전에 실패한다.
    [[nodiscard]] ShaderMetaCookProductResult BuildShaderMetaCookProduct(
        const ShaderMetaCookProductRequest& request);
}
