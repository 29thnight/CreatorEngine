#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // standalone `.asset` material 하나를 publication 직전의 완전 소유 산출물로
    // 바꾼다. 모델에 딸린 material 은 model CEMC 안의 subasset 이고 이쪽이
    // 아니다 — 여기는 `Assets/Materials/*.asset` 처럼 **파일 하나가 곧 재질**인
    // 경우만 다룬다.
    //
    // ★ artifact 는 원본 YAML 바이트 그대로다. texture/ShaderMeta 와 같은 자세.
    //
    // ★ **이것은 material 파서가 아니다. 의존 GUID 추출기다.**
    //
    //   재질의 의미는 `Material::reflect()` 가 소유한다. 그 클래스를 여기로
    //   끌어오면 도구가 렌더러 전체(Texture·DataSystem)를 링크하게 되고,
    //   그건 AssetCooker 를 따로 만든 이유를 지운다. 그래서 manifest 간선을
    //   그리는 데 필요한 두 가지만 읽는다 — `m_shaderMetaGuid` 와
    //   `m_propertyValues[].m_textureGuid`.
    //
    //   대신 **`m_shaderMetaGuid` 가 없거나 비정규면 실패한다.** 없으면 조용히
    //   의존 0 짜리 재질이 되어 "간선이 없는 것"과 "필드 이름이 바뀐 것"을
    //   구별할 수 없다. 스키마가 바뀌면 여기서 시끄럽게 깨져야 한다.

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

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    // 확장자 불일치, `.meta` 누락·비정규 GUID, YAML 파싱 실패,
    // `m_shaderMetaGuid` 누락·비정규, texture GUID 비정규, source-root 탈출,
    // 빈 파일은 모두 게시 전에 실패한다.
    [[nodiscard]] MaterialCookProductResult BuildMaterialCookProduct(
        const MaterialCookProductRequest& request);
}
