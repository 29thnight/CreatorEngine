#pragma once
// 신원 충돌 registry — ModelAssetBigBangCutoverPlan §2.4 (MBC1).
//
//   `(profile, epoch, modelAuthoringKey, kind, stableKey) ↔ UUID`는 전 corpus에서
//   bijection이어야 한다. 이 registry는 그 두 방향을 모두 지킨다:
//     · 같은 canonical tuple을 두 번 등록 → DuplicateTuple (같은 모델 안의 stable
//       key 중복이 대표 사례 — §2.3 "동일 kind 안에서 key가 중복되면 import 실패")
//     · 다른 tuple이 같은 UUID → UuidCollision (SHA-256이 충돌 불가능하다고
//       가정하지 않는다 — 게시 거부와 진단 artifact 대상)
//     · sidecar가 주장한 UUID가 재유도값과 다름 → RecomputeMismatch
//
// canonical tuple의 정본 표현은 BuildIdentityInputBytes가 낸 바이트열이다 —
// 프로필·epoch(namespace)·kind·key가 전부 그 안에 길이 접두로 들어 있으므로
// 바이트열이 같으면 tuple이 같고, 다르면 다르다. 문자열을 다시 조립해 비교하는
// 두 번째 표현을 두지 않는다(표현이 둘이면 언젠가 어긋난다).
//
// 이 클래스는 스레드 안전하지 않다. writer transaction(MBC3)이 자기 registry를
// 소유하고, corpus 전수 검사(MBC10)는 단일 스레드로 채운다.

#include "AssetIdentityProfile.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace assets
{
    enum class IdentityRegisterOutcome : std::uint8_t
    {
        Registered,
        DuplicateTuple,
        UuidCollision,
        RecomputeMismatch,
        InvalidInput,
    };
    [[nodiscard]] std::string_view ToString(IdentityRegisterOutcome outcome) noexcept;

    struct IdentityRegisterResult final
    {
        IdentityRegisterOutcome outcome{ IdentityRegisterOutcome::InvalidInput };
        Uuid::Uuid16 uuid{};                 // 유도값(InvalidInput이면 nil)
        std::string conflictingContext{};    // Duplicate/Collision/Mismatch의 상대
        IdentityIssue issue{ IdentityIssue::None }; // InvalidInput 사유
        std::string message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return outcome == IdentityRegisterOutcome::Registered;
        }
    };

    struct IdentityRegistryEntry final
    {
        std::vector<std::uint8_t> canonicalInput{};
        Uuid::Uuid16 uuid{};
        std::string context{}; // 진단용 — 어느 sidecar/어느 subasset이었나
    };

    class IdentityRegistry final
    {
    public:
        // writer 경로. 입력에서 유도해 등록한다. claimed가 nil이 아니면 유도값과
        // 대조하고 다르면 RecomputeMismatch로 거부한다(등록하지 않는다).
        [[nodiscard]] IdentityRegisterResult Register(const IdentityInput& input,
            std::string context, const Uuid::Uuid16& claimed = {});

        // 이미 유도한 값을 등록한다 — 바이트열과 UUID의 재유도 일치를 먼저 확인하고
        // (불일치면 RecomputeMismatch) tuple/UUID 충돌만 판정한다. 검사 도구가
        // sidecar에 적힌 값을 그대로 넣을 때 쓴다.
        [[nodiscard]] IdentityRegisterResult RegisterDerived(
            std::vector<std::uint8_t> canonicalInput, const Uuid::Uuid16& uuid,
            std::string context);

        // ★ 검사 전용 — 재유도 대조 없이 넣는다. UUID 충돌 분기(다른 tuple, 같은
        //   UUID)는 실제 SHA-256 충돌 없이는 만들 수 없으므로, selftest가 이 seam으로
        //   충돌 상태를 조립해 판정 코드를 태운다. 제품 writer는 부르지 않는다.
        void InsertUncheckedForTest(std::vector<std::uint8_t> canonicalInput,
            const Uuid::Uuid16& uuid, std::string context);

        [[nodiscard]] bool Contains(const Uuid::Uuid16& uuid) const noexcept;
        [[nodiscard]] const IdentityRegistryEntry* Find(
            const Uuid::Uuid16& uuid) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept { return m_entries.size(); }
        [[nodiscard]] std::span<const IdentityRegistryEntry> Entries() const noexcept
        {
            return m_entries;
        }

        // 전 항목을 다시 검산한다: 재유도 일치 · uuid→tuple 단사 · tuple→uuid 단사.
        // 실패 항목마다 한 줄을 outIssues에 남기고 false.
        [[nodiscard]] bool VerifyBijection(std::vector<std::string>& outIssues) const;

        void Clear() noexcept;

    private:
        [[nodiscard]] IdentityRegisterResult Insert(
            std::vector<std::uint8_t> canonicalInput, const Uuid::Uuid16& uuid,
            std::string context);

        std::vector<IdentityRegistryEntry> m_entries{};
        std::map<std::vector<std::uint8_t>, std::size_t> m_byInput{};
        std::map<Uuid::Uuid16, std::size_t> m_byUuid{};
    };
}
