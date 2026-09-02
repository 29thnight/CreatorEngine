#pragma once
// 프로젝트 identity epoch header — ModelAssetBigBangCutoverPlan §2.3·§3.1 (MBC2).
//
//   ModelId = V8(domain="model", namespace=identityEpochSeed, kind="model", key=authoringKey)
//
// epoch seed는 cutover 시 CSPRNG로 **한 번** 발급하는 256-bit 값이다. legacy GUID가
// 아니고 제품 AssetId로 노출하지 않는다. 프로젝트의 모든 ModelId가 이 seed를
// namespace로 삼으므로, seed가 바뀌면 프로젝트의 모델 신원 전체가 바뀐다 — 그것이
// "epoch"라는 이름의 뜻이다(§0.1-1: 옛 GUID 세계와 단절).
//
// ── 저장 위치(구현 시 확정 항목 §3.1) ──
//   `ProjectSetting/AssetIdentity.asset` 하나. 이유:
//     · ProjectSetting은 pak에 통째로 들어간다(PakHelper) → Player 런타임이 §4 load 1단계
//       "profile·epoch 검증"을 같은 파일로 한다.
//     · sidecar마다 seed를 복제하지 않는다 — 복제본이 어긋날 길을 없앤다. sidecar는
//       `identityEpoch` **이름**만 들고 header와 대조한다.
//   파일 형식은 YAML(다른 ProjectSetting과 같다).

#include "AssetIdentityProfile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{
    inline constexpr std::string_view kIdentityEpochHeaderFileName = "AssetIdentity.asset";
    inline constexpr std::uint32_t kIdentityEpochHeaderSchemaVersion = 1u;

    struct IdentityEpochHeader final
    {
        std::string identityProfile{ kIdentityProfile };
        std::string identityEpoch{};   // 사람이 읽는 epoch 이름(예: "2026-09-bigbang")
        IdentityEpochSeed identityEpochSeed{};
        std::string createdAt{};       // ISO-8601, 진단용(신원 계산에 들어가지 않는다)
    };

    enum class EpochHeaderIssueCode : std::uint8_t
    {
        InvalidDocument,
        MissingField,
        UnsupportedSchema,
        ProfileMismatch,
        InvalidEpochName,
        InvalidSeed,       // 64 소문자 hex가 아니다
        ZeroSeed,          // 전부 0 — 발급되지 않은 seed
    };

    struct EpochHeaderIssue final
    {
        EpochHeaderIssueCode code{ EpochHeaderIssueCode::InvalidDocument };
        std::string context{};
        std::string message{};
    };

    // CSPRNG(BCryptGenRandom, 시스템 선호 RNG). 실패는 실패다 — 약한 난수로 대체하지 않는다.
    [[nodiscard]] bool CreateIdentityEpochSeed(IdentityEpochSeed& out,
        std::string& outError) noexcept;

    // 프로필 일치·epoch 이름(비어 있지 않은 NFC UTF-8)·seed(0 아님)를 검사한다.
    [[nodiscard]] bool ValidateIdentityEpochHeader(const IdentityEpochHeader& header,
        std::vector<EpochHeaderIssue>& outIssues);

    // YAML 왕복. Write는 Validate를 통과한 header만 쓴다(실패면 빈 문자열).
    [[nodiscard]] std::string WriteIdentityEpochHeader(const IdentityEpochHeader& header);
    [[nodiscard]] bool ReadIdentityEpochHeader(std::string_view yaml,
        IdentityEpochHeader& out, std::vector<EpochHeaderIssue>& outIssues);

    [[nodiscard]] bool IsZeroSeed(const IdentityEpochSeed& seed) noexcept;
}
