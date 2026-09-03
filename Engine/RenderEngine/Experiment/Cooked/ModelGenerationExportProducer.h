#pragma once
// PHASE 3.75 MBC11 — 모델 cook의 단일 경로.
//
// legacy `ModelCookProducer`는 v1 sidecar(`guid` + subAssets schema 1)를 읽어 CEMC 하나를
// Derived에 구웠다. MBC3부터 모델의 정본 산출물은 authoring transaction이 게시한
// **generation**(`Library/ModelAssetGenerations/<ModelId>/<generation>/`)이고, 런타임은
// 그것만 읽는다(`DataSystem::LoadModelAssetGeneration`). 그래서 cook은 다시 굽지 않는다 —
// 이미 게시된 generation을 검증한 뒤 그대로 Derived로 내보낸다(파일 바이트 동일).
//
//   Derived/Models/<xx>/<ModelId>/<generation>/generation.asset   ← manifest entry(kind Model)
//   Derived/Models/<xx>/<ModelId>/<generation>/model.cemc
//   Derived/Models/<xx>/<ModelId>/<generation>/sidecar.meta
//   Derived/Models/<xx>/<ModelId>/<generation>/textures/<TextureId>.png …
//
// 게시되지 않은 모델(generation 부재)·sidecar와 어긋난 generation·closure 검증 실패는
// 전부 cook 실패다 — source에서 다시 만드는 폴백은 없다(§0.1-4).

#include "CookedAssetManifest.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace experiment::cooked
{
    struct ModelGenerationExportRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
        std::filesystem::path generationRoot{};      // <project>/Library/ModelAssetGenerations
        std::filesystem::path identityHeaderPath{};  // <project>/ProjectSetting/AssetIdentity.asset
    };

    struct ModelGenerationExportFile final
    {
        std::string artifactPath{};        // Derived/Models/xx/<id>/<gen>/<relative>
        std::vector<std::byte> bytes{};
    };

    struct ModelGenerationExportProduct final
    {
        AssetId modelAssetId{};
        std::uint64_t generation{};
        std::string recordArtifactPath{};  // generation.asset의 artifactPath
        std::vector<ModelGenerationExportFile> files{};
        CookedAssetManifestEntry manifestEntry{};
        // 모델 subasset entry — 재질(kind Material, record를 가리킴)·embedded texture
        // (kind Texture, textures/<TextureId>.png). 씬·재질 문서의 의존 GUID가 manifest
        // 안에서 해소되려면 legacy cook과 같이 이 entry들이 있어야 한다.
        std::vector<CookedAssetManifestEntry> subAssetEntries{};
        std::size_t materialCount{};
        std::size_t embeddedTextureCount{};
        std::size_t meshCount{};
        std::uint64_t embeddedTextureBytes{};
        std::uint64_t artifactBytes{};
    };

    struct ModelGenerationExportIssue final
    {
        std::string context{};
        std::string message{};
    };

    struct ModelGenerationExportResult final
    {
        std::optional<ModelGenerationExportProduct> product{};
        std::vector<ModelGenerationExportIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && issues.empty();
        }
    };

    [[nodiscard]] ModelGenerationExportResult BuildModelGenerationExportProduct(
        const ModelGenerationExportRequest& request);
}
