#pragma once
// 모델 자산 신원 프로필 `ce.uuidv8.sha256.v1` — ModelAssetBigBangCutoverPlan §2 (MBC1).
//
// ── 이 헤더가 계약이다 ──
//   UUIDv8(RFC 9562)은 해시 알고리즘도 입력 배치도 지정하지 않는다. 그러므로
//   "UUIDv8"이라는 이름은 호환의 근거가 아니고, 아래 바이트 계약 **전체**가
//   식별자의 정의다. 프로필 문자열·정규화·길이 접두·truncation·bit 설정 중
//   하나라도 다르면 다른 프로필이고, 그 값은 이 저장소의 자산 신원이 아니다.
//
//     IdentityInput :=
//       Bytes("ce.uuidv8.sha256.v1") || 0x00 ||
//       U32BE(len(domain))    || UTF8_NFC(domain) ||
//       U32BE(len(namespace)) || namespaceBytes  ||
//       U32BE(len(kind))      || UTF8_NFC(kind)   ||
//       U32BE(len(stableKey)) || UTF8_NFC(stableKey)
//     digest := SHA256(IdentityInput)
//     uuid   := digest[0..15]; uuid[6] := (uuid[6] & 0x0f) | 0x80;
//                              uuid[8] := (uuid[8] & 0x3f) | 0x80
//
//   규칙(§2.2):
//     · 문자열은 UTF-8 NFC, NUL 종단자 없음. 길이는 **바이트 수**를 U32BE로.
//     · 이 계층은 정규화를 **하지 않는다** — NFC가 아니면 거부한다(fail-closed).
//       정규화는 writer가 입력을 만들 때의 책임이고 NormalizeUtf8Nfc가 그 도구다.
//       유도 함수 안에서 조용히 정규화하면 "같은 입력"의 뜻이 흐려진다.
//     · namespace는 문자열이 아니라 바이트다(16바이트 UUID 또는 32바이트 epoch seed).
//     · 임의 lower-case·trim·경로 구분자 치환은 여기서 하지 않는다.
//
// ── 기존 GUID와의 관계 ──
//   없다. 기존 UUIDv4 GUID나 pseudo-v5(SHA-1, v4 nibble 스탬프) 값은 namespace로도
//   key로도 들어오지 않는다(§0.1-1). 이 헤더는 Uuid::FromName을 부르지 않는다.
//
// ── 위치 ──
//   `Engine/RenderEngine/Assets/`는 experiment 네임스페이스가 아니다(§5.1 마지막 문단).
//   cutover 뒤 제품 소비자가 직접 include하는 정식 계층이다.

#include "Uuid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets
{
    inline constexpr std::string_view kIdentityProfile = "ce.uuidv8.sha256.v1";

    // §2.3 계층 어휘. **문자열**이 계약이다 — enum 값은 코드 편의일 뿐이고
    // 바이트열에는 아래 이름이 들어간다.
    inline constexpr std::string_view kDomainModel = "model";
    inline constexpr std::string_view kDomainSubAsset = "subasset";
    inline constexpr std::string_view kKindModel = "model";

    enum class SubAssetKind : std::uint8_t
    {
        Mesh,
        Material,
        Texture,
        Skeleton,
        Animation,
    };
    inline constexpr std::size_t kSubAssetKindCount = 5u;

    [[nodiscard]] std::string_view ToKindName(SubAssetKind kind) noexcept;
    [[nodiscard]] bool TryParseKindName(std::string_view name,
        SubAssetKind& outKind) noexcept;

    // cutover 시 CSPRNG로 한 번 발급하는 256-bit seed. legacy GUID가 아니고
    // 제품 AssetId로 노출하지 않는다(§2.3).
    using IdentityEpochSeed = std::array<std::uint8_t, 32>;
    inline constexpr std::size_t kEpochSeedBytes = 32u;
    inline constexpr std::size_t kUuidBytes = 16u;

    // ★ 뷰(string_view·span)만 든다 — 저장은 호출자 몫이고, 유도 함수가 돌아온 뒤에는
    //   참조하지 않는다. 임시 객체를 바로 묶으면 표현식 끝에서 dangling이다(selftest가
    //   그 함정을 한 번 밟았다). writer는 자기 문자열·바이트를 지역/멤버에 두고 넘긴다.
    struct IdentityInput final
    {
        std::string_view domain{};
        std::span<const std::uint8_t> namespaceBytes{};
        std::string_view kind{};
        std::string_view stableKey{};
    };

    enum class IdentityIssue : std::uint8_t
    {
        None,
        EmptyDomain,
        EmptyNamespace,
        EmptyKind,
        EmptyStableKey,
        InvalidUtf8,          // RFC 3629 위반(overlong·surrogate·절단·>U+10FFFF)
        NotNfc,               // 잘 형성됐지만 NFC가 아니다 — 정규화는 writer 책임
        NormalizationFailed,  // OS 정규화 질의 자체가 실패 — fail-closed
        FieldTooLong,         // U32BE 길이 접두 범위 밖
        NamespaceLength,      // 계층 유도가 요구한 namespace 길이가 아니다
        NamespaceNotV8,       // subasset namespace(ModelId)가 이 프로필의 UUIDv8이 아니다
        EmptyProfile,
    };
    [[nodiscard]] std::string_view ToString(IdentityIssue issue) noexcept;

    struct IdentityDerivation final
    {
        Uuid::Uuid16 uuid{};
        IdentityIssue issue{ IdentityIssue::None };
        std::string context{}; // 어느 필드/어느 위치가 걸렸나

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return issue == IdentityIssue::None;
        }
    };

    // §2.2 바이트열을 만든다. 검증에 걸리면 out은 비우고 false다.
    // canonical tuple의 **정본 표현**이기도 하다 — registry가 이 바이트열을
    // 키로 쓴다(같은 바이트열 = 같은 tuple).
    [[nodiscard]] bool BuildIdentityInputBytes(const IdentityInput& input,
        std::vector<std::uint8_t>& out, IdentityIssue& outIssue,
        std::string& outContext) noexcept;

    // 바이트열에서 UUID만 낸다(SHA-256 → 16바이트 → v8/variant bit). 바이트열은
    // 프로필을 이미 포함하므로 프로필 인자가 없다. registry의 재검산이 쓴다.
    [[nodiscard]] Uuid::Uuid16 DeriveUuidFromIdentityInputBytes(
        std::span<const std::uint8_t> canonicalInput) noexcept;

    [[nodiscard]] IdentityDerivation DeriveIdentity(
        const IdentityInput& input) noexcept;

    // ★ 변이 검사 전용. 프로필 문자열을 바꿔 유도한다 — "프로필이 다르면 다른
    //   신원"을 selftest가 단정하기 위한 것이다. 제품 writer는 이 오버로드를
    //   부르지 않는다(정적 게이트 대상).
    [[nodiscard]] IdentityDerivation DeriveIdentityWithProfile(
        std::string_view profile, const IdentityInput& input) noexcept;

    // ── §2.3 계층 ─────────────────────────────────────────────────────────
    // ModelId = V8(domain="model", namespace=epochSeed, kind="model", key=authoringKey)
    [[nodiscard]] IdentityDerivation DeriveModelId(const IdentityEpochSeed& epochSeed,
        std::string_view modelAuthoringKey) noexcept;

    // XId = V8(domain="subasset", namespace=ModelId, kind=<kind>, key=stableKey)
    // modelId가 이 프로필의 UUIDv8이 아니면 NamespaceNotV8로 거부한다 — legacy
    // v4 GUID를 namespace로 흘려 넣는 경로를 타입이 아니라 값에서 막는다.
    [[nodiscard]] IdentityDerivation DeriveSubAssetId(const Uuid::Uuid16& modelId,
        SubAssetKind kind, std::string_view stableKey) noexcept;

    // ── 판정·표기 ─────────────────────────────────────────────────────────
    // nil이 아니고 version nibble 8, RFC variant 10xx.
    [[nodiscard]] bool IsUuidV8(const Uuid::Uuid16& value) noexcept;

    // 소문자 8-4-4-4-12의 UUIDv8만 받는다. 대문자·brace·무하이픈·v4·v5는 거부 —
    // experiment::TryParseCanonicalAssetId(v4)와 대칭이되 버전만 다르다.
    [[nodiscard]] bool TryParseCanonicalUuidV8(std::string_view text,
        Uuid::Uuid16& out) noexcept;

    // ── 문자열 규약 ───────────────────────────────────────────────────────
    [[nodiscard]] bool IsWellFormedUtf8(std::string_view text) noexcept;

    // NFC 판정. ASCII만이면 OS를 부르지 않는다. 판정 불가(OS 실패)는 false +
    // NormalizationFailed — "모른다"를 "맞다"로 읽지 않는다.
    [[nodiscard]] bool IsUtf8Nfc(std::string_view text,
        IdentityIssue& outIssue) noexcept;

    // writer용 도구. 잘 형성된 UTF-8을 NFC로 바꾼다. 유도 함수는 이것을 부르지
    // 않는다 — 정규화는 입력을 만드는 쪽의 명시적 단계다.
    [[nodiscard]] bool NormalizeUtf8Nfc(std::string_view text, std::string& out,
        std::string& outError);
}
