#pragma once
// subasset stable key — ModelAssetBigBangCutoverPlan §2.3 (MBC2).
//
// stable key는 "원본 안에서 이 요소를 다음 임포트에도 같은 것으로 알아볼 근거"다.
// 우선순위(§2.3):
//   1. exporter가 보존하는 명시적 persistent ID(`extras.creatorEngineId` 등)   → `exporter:<id>`
//   2. 원본 DCC/exporter persistent object ID                                    → `exporter:<id>`
//   3. 타입·계층·명시 이름으로 만든 검증 가능한 semantic key                     → `name:<NFC 이름>`
//   4. schema v2에 한 번 기록한 새 immutable authoring key                        → `authoring:<64hex>`
//
// 금지(§2.3): 배열 ordinal만 쓴 키(`gltf/material/0`), 표시 이름만으로 중복을 못 가르는
// 키, 파일명 기반 추측. 같은 kind 안에서 key가 중복되거나 안정성을 증명할 수 없으면
// import를 실패시킨다.
//
// ── 실측이 정한 것 (MBC0 기준선 §2.1) ──
//   corpus 14개 모델 어디에도 exporter persistent ID가 없다. 이름은 대부분 유일하지만
//   `scene.glb`는 재질 25·이미지 8·메시 8이 전부 무명이다. 그래서 3(semantic)이 주력이고
//   4(authoring)가 반드시 있어야 하며, 4의 핵심은 **재결합 규칙**이다:
//     · authoring key는 요소의 콘텍츠 지문(SHA-256)과 함께 sidecar에 기록된다.
//     · 재임포트 때 같은 kind·같은 지문의 prior key를 되찾는다. 지문이 같은(= 바이트
//       동일) 요소가 여럿이면 binding 순으로 짝짓는다 — 서로 바꿔 끼워도 관측 불가하므로
//       안전하다(scene.glb 무명 메시 103개에 동일 지오메트리가 여럿이다).
//     · prior에 없는 지문의 요소는 새 key. 현재에 없는 지문의 prior key는 은퇴(경고).
//     · **둘이 동시에** 있으면(고아 prior + 새 지문 요소) "내용 변경인지 삭제+추가인지
//       증명할 수 없다" — AuthoringRebindAmbiguous **오류**로 import를 실패시킨다(§2.3).
//   ordinal(`gltf/material/3`)은 진단용 `binding`으로만 남긴다 — 신원 계산에 안 들어간다.
//
// 이 계층은 experiment::importer::ImportedScene을 **입력**으로만 받는다(임포터 IR은
// cutover 뒤에도 남는 것이고, 정식 계층으로의 승격은 §5.1 대상이다).

#include "AssetIdentityProfile.h"
#include "../Experiment/Import/ImportedScene.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{
    inline constexpr std::string_view kStableKeyOriginExporter = "exporter:";
    inline constexpr std::string_view kStableKeyOriginName = "name:";
    inline constexpr std::string_view kStableKeyOriginAuthoring = "authoring:";
    inline constexpr std::string_view kFingerprintPrefix = "sha256:";
    inline constexpr std::size_t kAuthoringKeyBytes = 32u;

    enum class StableKeyOrigin : std::uint8_t
    {
        Exporter,
        Semantic,
        Authoring,
    };
    [[nodiscard]] std::string_view ToString(StableKeyOrigin origin) noexcept;

    // 텍스트 문법 검사. 허용 접두 셋 중 하나로 시작하고, 값이 비어 있지 않고 NFC이며,
    // authoring 값은 64자 소문자 hex. ordinal 패턴(`x/y/3`, `3`)은 접두가 없어 여기서 걸린다.
    [[nodiscard]] bool TryParseStableKey(std::string_view text, StableKeyOrigin& outOrigin,
        std::string& outError) noexcept;
    [[nodiscard]] bool IsForbiddenOrdinalKey(std::string_view text) noexcept;

    // sidecar v2가 기록하는 subasset 한 줄 — ModelSidecarV2가 이 타입을 그대로 쓴다.
    struct ModelSubAssetRecord final
    {
        SubAssetKind kind{ SubAssetKind::Material };
        std::string stableKey{};           // 신원 입력
        Uuid::Uuid16 assetId{};            // DeriveSubAssetId(modelId, kind, stableKey)
        std::string binding{};             // 진단: 원본 안 위치(gltf/material/3) — 신원 아님
        std::string name{};                // 진단: 표시 이름
        std::string fingerprint{};         // `sha256:<64hex>` — authoring 재결합·stale 판정
    };

    // 임포트 결과에서 뽑은 "신원 후보 요소" 하나. 어댑터가 ImportedScene을 이 형태로
    // 평탄화하고, 규칙 엔진은 이 형태만 본다(규칙이 임포터 IR 필드에 직접 묶이지 않게).
    struct StableKeyElement final
    {
        SubAssetKind kind{ SubAssetKind::Material };
        std::size_t index{};               // 해당 kind 배열 안의 위치(binding 진단용)
        std::string persistentId{};        // 우선순위 1·2 — 오늘은 임포터가 채우지 않는다
        std::string name{};                // 우선순위 3 후보
        std::string binding{};             // 진단
        std::string fingerprint{};         // `sha256:<64hex>`
    };

    enum class StableKeyIssueCode : std::uint8_t
    {
        InvalidPersistentId,        // exporter id가 비어 있거나 NFC가 아님
        DuplicatePersistentId,      // 같은 kind 안에서 exporter id 중복 → 실패
        NameNotNfc,                 // 이름이 NFC가 아니라 semantic key로 못 쓴다 → authoring으로
        AuthoringRebindAmbiguous,   // 옛 authoring key를 되찾을 수 없고 새 무명 요소가 있다 → 실패
        AuthoringKeyRetired,        // 옛 authoring key에 맞는 요소가 사라졐다(경고)
        DuplicateStableKey,         // 최종 key 중복(구조상 불가능해야 한다 — 발생하면 결함)
        SeedFailure,                // authoring key CSPRNG 실패
    };
    [[nodiscard]] bool IsStableKeyError(StableKeyIssueCode code) noexcept;

    struct StableKeyIssue final
    {
        StableKeyIssueCode code{ StableKeyIssueCode::DuplicateStableKey };
        std::string context{};
        std::string message{};
    };

    struct StableKeyAssignment final
    {
        SubAssetKind kind{ SubAssetKind::Material };
        std::size_t index{};
        StableKeyOrigin origin{ StableKeyOrigin::Semantic };
        std::string stableKey{};
        std::string binding{};
        std::string name{};
        std::string fingerprint{};
        bool reboundFromPrior{};           // authoring key를 이전 sidecar에서 되찾았다
    };

    struct StableKeyResult final
    {
        std::vector<StableKeyAssignment> assignments{};
        std::vector<StableKeyIssue> issues{};

        [[nodiscard]] bool Succeeded() const noexcept;
        [[nodiscard]] std::size_t CountOrigin(SubAssetKind kind, StableKeyOrigin origin) const noexcept;
    };

    // authoring key 발급기 — 기본은 CSPRNG. 검사가 결정적 값으로 갈아 끼울 수 있다.
    using AuthoringKeyFactory = bool (*)(std::array<std::uint8_t, kAuthoringKeyBytes>& out,
        std::string& outError);
    [[nodiscard]] bool CreateAuthoringKeyBytes(
        std::array<std::uint8_t, kAuthoringKeyBytes>& out, std::string& outError) noexcept;
    [[nodiscard]] std::string MakeAuthoringStableKey(
        std::span<const std::uint8_t, kAuthoringKeyBytes> bytes);

    // ImportedScene → 요소 목록. 외부 텍스처(자기 .meta를 가진 것)는 subasset이 아니므로
    // 뺀다. 지문은 여기서 계산한다(재질=속성+슬롯 텍스처 지문, 텍스처=바이트, 메시=
    // 위치+인덱스, 스켈레톤=joint 이름+inverse bind, 애니메이션=채널 타깃 이름+키).
    [[nodiscard]] std::vector<StableKeyElement> CollectStableKeyElements(
        const experiment::importer::ImportedScene& scene);

    // 규칙 엔진. prior는 이전 sidecar v2의 subasset 기록(없으면 빈 span).
    [[nodiscard]] StableKeyResult DeriveModelStableKeys(
        std::span<const StableKeyElement> elements,
        std::span<const ModelSubAssetRecord> prior,
        AuthoringKeyFactory authoringKeyFactory = &CreateAuthoringKeyBytes);
}
