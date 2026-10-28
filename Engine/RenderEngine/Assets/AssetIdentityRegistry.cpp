#include "../../Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../../Containers/ContainerAPI.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Uuid.h"
#else
import Creator.Core.Uuid;
#endif
#include "AssetIdentityRegistry.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets
{
    std::string_view ToString(IdentityRegisterOutcome outcome) noexcept
    {
        switch (outcome)
        {
        case IdentityRegisterOutcome::Registered:        return "registered";
        case IdentityRegisterOutcome::DuplicateTuple:    return "duplicate-tuple";
        case IdentityRegisterOutcome::UuidCollision:     return "uuid-collision";
        case IdentityRegisterOutcome::RecomputeMismatch: return "recompute-mismatch";
        case IdentityRegisterOutcome::InvalidInput:      return "invalid-input";
        }
        return "unknown";
    }

    IdentityRegisterResult IdentityRegistry::Register(const IdentityInput& input,
        std::string context, const uuid::uuid16& claimed)
    {
        IdentityRegisterResult result;
        ce::dynamic_array<std::uint8_t> canonical;
        std::string issueContext;
        if (!BuildIdentityInputBytes(input, canonical, result.issue, issueContext))
        {
            result.outcome = IdentityRegisterOutcome::InvalidInput;
            result.message = std::string(ToString(result.issue)) + " @" + issueContext
                + " (" + context + ")";
            return result;
        }

        const uuid::uuid16 derived = DeriveUuidFromIdentityInputBytes(canonical);
        if (!claimed.is_nil() && claimed != derived)
        {
            result.outcome = IdentityRegisterOutcome::RecomputeMismatch;
            result.uuid = derived;
            result.conflictingContext = context;
            result.message = "claimed " + uuid::to_string(claimed) + " != derived "
                + uuid::to_string(derived) + " (" + context + ")";
            return result;
        }
        return Insert(std::move(canonical), derived, std::move(context));
    }

    IdentityRegisterResult IdentityRegistry::RegisterDerived(
        ce::dynamic_array<std::uint8_t> canonicalInput, const uuid::uuid16& uuid,
        std::string context)
    {
        IdentityRegisterResult result;
        if (canonicalInput.empty() || uuid.is_nil())
        {
            result.outcome = IdentityRegisterOutcome::InvalidInput;
            result.issue = canonicalInput.empty()
                ? IdentityIssue::EmptyProfile : IdentityIssue::EmptyNamespace;
            result.message = "empty canonical input or nil uuid (" + context + ")";
            return result;
        }
        const uuid::uuid16 derived = DeriveUuidFromIdentityInputBytes(canonicalInput);
        if (derived != uuid)
        {
            result.outcome = IdentityRegisterOutcome::RecomputeMismatch;
            result.uuid = derived;
            result.conflictingContext = context;
            result.message = "claimed " + uuid::to_string(uuid) + " != derived "
                + uuid::to_string(derived) + " (" + context + ")";
            return result;
        }
        return Insert(std::move(canonicalInput), uuid, std::move(context));
    }

    void IdentityRegistry::InsertUncheckedForTest(
        ce::dynamic_array<std::uint8_t> canonicalInput, const uuid::uuid16& uuid,
        std::string context)
    {
        const std::size_t index = m_entries.size();
        m_byInput.emplace(canonicalInput, index);
        m_byUuid.emplace(uuid, index);
        m_entries.push_back({ std::move(canonicalInput), uuid, std::move(context) });
    }

    IdentityRegisterResult IdentityRegistry::Insert(
        ce::dynamic_array<std::uint8_t> canonicalInput, const uuid::uuid16& uuid,
        std::string context)
    {
        IdentityRegisterResult result;
        result.uuid = uuid;

        // tuple 중복을 UUID 충돌보다 먼저 본다 — 같은 tuple은 당연히 같은 UUID를
        // 내므로, 순서를 바꾸면 모든 중복이 "충돌"로 보고돼 사유가 흐려진다.
        if (const auto found = m_byInput.find(canonicalInput); found != m_byInput.end())
        {
            result.outcome = IdentityRegisterOutcome::DuplicateTuple;
            result.conflictingContext = m_entries[found->second].context;
            result.message = "duplicate canonical tuple: " + context + " vs "
                + result.conflictingContext;
            return result;
        }
        if (const auto found = m_byUuid.find(uuid); found != m_byUuid.end())
        {
            result.outcome = IdentityRegisterOutcome::UuidCollision;
            result.conflictingContext = m_entries[found->second].context;
            result.message = "uuid collision " + uuid::to_string(uuid) + ": "
                + context + " vs " + result.conflictingContext;
            return result;
        }

        const std::size_t index = m_entries.size();
        m_byInput.emplace(canonicalInput, index);
        m_byUuid.emplace(uuid, index);
        m_entries.push_back({ std::move(canonicalInput), uuid, std::move(context) });
        result.outcome = IdentityRegisterOutcome::Registered;
        return result;
    }

    bool IdentityRegistry::Contains(const uuid::uuid16& uuid) const noexcept
    {
        return m_byUuid.contains(uuid);
    }

    const IdentityRegistryEntry* IdentityRegistry::Find(
        const uuid::uuid16& uuid) const noexcept
    {
        const auto found = m_byUuid.find(uuid);
        return found == m_byUuid.end() ? nullptr : &m_entries[found->second];
    }

    bool IdentityRegistry::VerifyBijection(ce::dynamic_array<std::string>& outIssues) const
    {
        const std::size_t before = outIssues.size();

        // 색인이 아니라 **항목 벡터**를 다시 훑는다. 색인은 삽입 시점의 판정을
        // 반영할 뿐이고, 이 함수의 목적은 그 판정과 무관하게 현재 내용을 검산하는
        // 것이다(InsertUncheckedForTest가 넣은 충돌도 여기서 드러나야 한다).
        ce::engine_ordered_map<ce::dynamic_array<std::uint8_t>, std::size_t> seenInput;
        ce::engine_ordered_map<uuid::uuid16, std::size_t> seenUuid;
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const IdentityRegistryEntry& entry = m_entries[index];
            const uuid::uuid16 derived =
                DeriveUuidFromIdentityInputBytes(entry.canonicalInput);
            if (derived != entry.uuid)
            {
                outIssues.push_back("recompute mismatch: " + entry.context
                    + " stored " + uuid::to_string(entry.uuid) + " derived "
                    + uuid::to_string(derived));
            }
            if (const auto dup = seenInput.find(entry.canonicalInput);
                dup != seenInput.end())
            {
                outIssues.push_back("duplicate tuple: " + entry.context + " vs "
                    + m_entries[dup->second].context);
            }
            else
            {
                seenInput.emplace(entry.canonicalInput, index);
            }
            if (const auto dup = seenUuid.find(entry.uuid); dup != seenUuid.end())
            {
                if (m_entries[dup->second].canonicalInput != entry.canonicalInput)
                {
                    outIssues.push_back("uuid collision " + uuid::to_string(entry.uuid)
                        + ": " + entry.context + " vs " + m_entries[dup->second].context);
                }
            }
            else
            {
                seenUuid.emplace(entry.uuid, index);
            }
        }
        return outIssues.size() == before;
    }

    void IdentityRegistry::Clear() noexcept
    {
        m_entries.clear();
        m_byInput.clear();
        m_byUuid.clear();
    }
}
