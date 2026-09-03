#pragma once
// PHASE 3.75 MBC3 — model identity의 유일한 디스크 writer.
//
// source decode, stable-key 결합, UUIDv8 closure, schema-v2 sidecar, cooked
// generation 작성/재판독과 게시를 한 호출에서 끝낸다. canonical sidecar는
// generation directory가 원자 rename된 뒤 마지막 commit record로 교체된다.
// 어느 단계에서 실패해도 기존 sidecar와 기존 generation은 건드리지 않는다.

#include "AssetIdentityProfile.h"
#include "ModelAssetPhaseTiming.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace assets
{
    enum class ModelAuthoringFailurePoint : std::uint8_t
    {
        None,
        AfterDecode,
        AfterIdentity,
        AfterStageWrite,
        AfterStageValidation,
        AfterGenerationPublish,
    };

    struct ModelAssetAuthoringRequest final
    {
        std::filesystem::path assetRoot{};
        std::filesystem::path sourcePath{};
        std::filesystem::path identityHeaderPath{};
        std::filesystem::path generationRoot{};

        // 회귀 검사의 단계별 원자성 검증에만 사용한다. 제품 호출은 None이다.
        ModelAuthoringFailurePoint failurePoint{ ModelAuthoringFailurePoint::None };
    };

    struct ModelAssetAuthoringIssue final
    {
        std::string stage{};
        std::string message{};
    };

    struct ModelAssetAuthoringResult final
    {
        Uuid::Uuid16 modelAssetId{};
        std::uint64_t generation{};
        std::filesystem::path sidecarPath{};
        std::filesystem::path generationPath{};
        std::size_t subAssetCount{};
        std::size_t materialCount{};
        std::size_t embeddedTextureCount{};
        std::vector<ModelAssetAuthoringIssue> issues{};
        // 단계별 경과(ms, 순서대로) — 진단 전용. 실패 시엔 도달한 단계까지만 남는다.
        ModelAssetPhaseTimeline phases{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return issues.empty() && IsUuidV8(modelAssetId)
                && generation != 0u && !sidecarPath.empty()
                && !generationPath.empty();
        }
    };

    [[nodiscard]] bool IsModelAuthoringSource(
        const std::filesystem::path& sourcePath) noexcept;

    // 유일한 model sidecar writer. 기존 v1 sidecar는 보존할 신원으로 읽지 않고
    // non-identity 설정만 schema v2 출력에 복사한다. 기존 v2 sidecar만 prior
    // stable key와 generation의 정본으로 인정한다.
    [[nodiscard]] ModelAssetAuthoringResult AuthorModelAsset(
        const ModelAssetAuthoringRequest& request);
}
