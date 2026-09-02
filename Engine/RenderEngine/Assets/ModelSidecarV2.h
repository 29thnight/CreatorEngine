#pragma once
// model sidecar schema v2 — ModelAssetBigBangCutoverPlan §3.1 (MBC2).
//
//   schemaVersion: 2
//   identityProfile: ce.uuidv8.sha256.v1
//   identityEpoch: <epoch 이름 — seed는 ProjectSetting/AssetIdentity.asset에만 있다>
//   authoringKey: authoring:<64hex> | exporter:<id>      ← modelAuthoringKey(§2.3)
//   assetId: <uuidv8 = DeriveModelId(epochSeed, authoringKey)>
//   generation: <monotonic integer>
//   sourceFingerprint: sha256:<64hex>                     ← 원본 파일 바이트
//   subAssets:
//     - kind: material | texture | mesh | skeleton | animation
//       stableKey: name:<…> | exporter:<…> | authoring:<64hex>
//       assetId: <uuidv8 = DeriveSubAssetId(model assetId, kind, stableKey)>
//       binding: gltf/material/1        # 진단 — 신원 아님
//       name: MI_Hero_GU_F_Mythic       # 진단
//       fingerprint: sha256:<64hex>     # authoring 재결합·stale 판정
//
// 규칙:
//   · legacy 최상위 `guid:`는 v2 문서에 없다. 있으면 읽기가 거부한다(§8.1 "legacy GUID
//     alias 0"). v1(`subAssets.schemaVersion: 1`)은 LegacySchema로 거부 — MBC4 offline
//     migrator의 입력이며 런타임 reader의 입력이 아니다.
//   · Read는 문법·형식만 본다. 신원이 맞는지(재유도 일치·bijection)는 epoch header가
//     필요하므로 ValidateModelSidecarV2Closure가 따로 본다. 둘을 합치면 "header 없음"과
//     "문서가 틀림"을 못 가른다.
//   · Write는 importSettings·ModelImporter 같은 다른 최상위 키를 보존한다(existingYaml).
//     신원 키 6종과 subAssets는 문서 값으로 **덮고**, `guid`는 지운다.
//   · 여기는 코덱이다. 디스크에 쓰는 것(원자 게시·temp generation)은 MBC3의
//     ModelAssetAuthoringTransaction이다.

#include "AssetIdentityEpoch.h"
#include "ModelStableKeys.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{
    inline constexpr std::uint32_t kModelSidecarSchemaVersion = 2u;

    struct ModelSidecarV2 final
    {
        std::string identityProfile{ kIdentityProfile };
        std::string identityEpoch{};
        std::string authoringKey{};        // exporter:<id> | authoring:<64hex>
        Uuid::Uuid16 assetId{};
        std::uint64_t generation{};
        std::string sourceFingerprint{};   // sha256:<64hex>
        std::vector<ModelSubAssetRecord> subAssets{};
    };

    enum class SidecarIssueCode : std::uint8_t
    {
        InvalidDocument,
        MissingField,
        LegacySchema,          // v1 layout — migrator 대상
        UnsupportedSchema,
        LegacyGuidField,       // 최상위 guid: 가 남아 있다
        ProfileMismatch,
        EpochMismatch,
        InvalidAssetId,        // canonical UUIDv8이 아니다
        InvalidStableKey,      // 문법 위반·ordinal·NFC 아님
        InvalidModelKey,       // model authoringKey는 exporter:|authoring: 만
        InvalidKind,
        InvalidGeneration,
        InvalidFingerprint,
        DuplicateStableKey,
        DuplicateAssetId,
        RecomputeMismatch,     // 적힌 assetId ≠ 재유도
        Collision,             // 다른 tuple, 같은 UUID
    };
    [[nodiscard]] std::string_view ToString(SidecarIssueCode code) noexcept;

    struct SidecarIssue final
    {
        SidecarIssueCode code{ SidecarIssueCode::InvalidDocument };
        std::string context{};
        std::string message{};
    };

    [[nodiscard]] bool IsFingerprintText(std::string_view text) noexcept;

    // 문법·형식 검사. 실패면 out은 건드리지 않는다.
    [[nodiscard]] bool ReadModelSidecarV2(std::string_view yaml, ModelSidecarV2& out,
        std::vector<SidecarIssue>& outIssues);

    // existingYaml의 다른 최상위 키를 보존하며 v2 신원 키를 덮어 쓴다. 문서가 형식
    // 검사를 통과하지 못하면 빈 문자열.
    [[nodiscard]] std::string WriteModelSidecarV2(const ModelSidecarV2& document,
        std::string_view existingYaml, std::vector<SidecarIssue>& outIssues);

    // header 대조·모델 id 재유도·subasset id 재유도·bijection(IdentityRegistry).
    [[nodiscard]] bool ValidateModelSidecarV2Closure(const ModelSidecarV2& document,
        const IdentityEpochHeader& header, std::vector<SidecarIssue>& outIssues);

    // stable key 배정 결과에서 문서를 만든다(신원 전부 유도). 실패면 false.
    [[nodiscard]] bool BuildModelSidecarV2(const IdentityEpochHeader& header,
        std::string_view modelAuthoringKey, std::uint64_t generation,
        std::string_view sourceFingerprint, std::span<const StableKeyAssignment> assignments,
        ModelSidecarV2& out, std::vector<SidecarIssue>& outIssues);

    // 원본 파일 바이트의 `sha256:<64hex>`.
    [[nodiscard]] std::string MakeSourceFingerprint(std::span<const std::uint8_t> bytes);

    // 새 모델 authoring key `authoring:<64hex>`(CSPRNG). 실패면 빈 문자열.
    [[nodiscard]] std::string CreateModelAuthoringKey(std::string& outError);
}
