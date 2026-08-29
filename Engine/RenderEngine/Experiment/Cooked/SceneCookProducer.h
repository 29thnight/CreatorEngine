#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // `.creator`(scene) 와 `.prefab` 하나를 publication 직전의 완전 소유
    // 산출물로 바꾼다. 둘은 같은 YAML 모양이고 kind 만 다르다.
    //
    // ★ artifact 는 원본 YAML 바이트 그대로다. 앞선 producer 들과 같은 자세.
    //
    // ★ **이것은 scene 파서가 아니다. 의존 GUID 추출기다.**
    //   scene 의 의미는 리플렉션 역직렬화가 소유한다. 여기서는 manifest 간선을
    //   그리는 데 필요한 키만 재귀적으로 훑는다.
    //
    // ── 실측 (2026-08-29, scene 14 · prefab 9) ────────────────────────────
    //
    //   | 참조        | 형태               | 수 |
    //   |-------------|--------------------|----|
    //   | model       | `m_fileGuid` GUID  | 10 |
    //   | prefab      | GUID               |  9 |
    //   | texture     | **파일명 문자열**  | 17 |
    //   | BT/blackboard | GUID             |  2 |
    //
    // ★ **`m_fileGuid` 는 재질 GUID 가 아니라 모델 GUID 다.** 인라인된
    //   `m_Material` 안에 있어서 재질 것처럼 보이지만, `MeshRenderer` 가 그것을
    //   `LoadModelGUID` 에 넘긴다. 메시는 그 모델 **안에서 이름으로** 고르므로
    //   서브에셋 조회이고, 주소 단위는 모델 artifact 가 맞다.
    //
    // ★ **텍스처는 아직 GUID 로 참조되지 않는다.** 씬의 인라인 재질에는
    //   `m_propertyValues`/`m_textureGuid` 가 **0건**이고 legacy
    //   `m_baseColorTexName` 같은 파일명 필드만 있다. 런타임은 GUID 우선 ·
    //   이름 폴백(`DataSystem::FinalizeMaterialRuntime`)이라 지금은 폴백이
    //   그것을 나른다.
    //
    //   그래서 이름 참조는 **간선으로 그리지 않고 센다.** 없는 GUID 를
    //   지어내면 §3.6.1 이 죽이려는 평탄화(stem 충돌 17건)를 쿠킹 안으로
    //   다시 들여오는 것이다. D5-c 의 "source path 탐색 없이"는 이 수가 0 이
    //   되어야 성립한다 — 그건 저작 데이터 이주이지 쿠킹의 일이 아니다.
    //
    // ★ BT/blackboard GUID 도 마찬가지로 세기만 한다. producer 가 없어서
    //   간선을 그리면 해소되지 않는다.

    struct SceneCookProductRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
    };

    struct SceneCookProduct final
    {
        AssetId sceneAssetId{};
        CookedAssetKind kind{ CookedAssetKind::Scene };
        std::string artifactPath{};
        std::vector<std::byte> artifactBytes{};
        CookedAssetManifestEntry manifestEntry{};

        std::size_t modelEdges{};
        std::size_t prefabEdges{};
        std::size_t textureEdges{};

        // ★ 그리지 못한 것들. 아무도 안 읽는 필드로 두지 않는다 —
        //   AssetCooker 가 요약에 찍고, 계획서가 D5-c 판정에 쓴다.
        std::size_t legacyTextureNameReferences{};
        std::size_t unproducedGuidReferences{};
    };

    struct SceneCookProductIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct SceneCookProductResult final
    {
        std::optional<SceneCookProduct> product{};
        std::vector<SceneCookProductIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    // 확장자 불일치, `.meta` 누락·비정규 GUID, YAML 파싱 실패, 비정규 참조
    // GUID, source-root 탈출, 빈 파일은 모두 게시 전에 실패한다.
    [[nodiscard]] SceneCookProductResult BuildSceneCookProduct(
        const SceneCookProductRequest& request);
}
