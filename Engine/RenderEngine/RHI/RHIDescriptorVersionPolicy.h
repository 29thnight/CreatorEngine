#pragma once

#include "RHIResourceTypes.h"

#include <cstdint>
#include <limits>
#include <vector>

// Transient descriptor의 네이티브 payload는 DX12 shader-visible heap page와
// Vulkan descriptor pool로 서로 다르다. 공통 계층은 payload를 소유하지 않고,
// "어느 버전을 다시 써도 되는가"만 completion point로 판정한다.
//
// 한 버전은 recording 동안만 CPU가 쓸 수 있다. 제출 뒤에는 immutable이며,
// Pending completion이 지나기 전에는 Available로 돌아오지 않는다. completion 0은
// 제출됐지만 완료를 증명할 수 없는 경우라 teardown 전까지 Quarantined에 둔다.
enum class RHIDescriptorVersionState : uint8_t
{
    Available,
    Recording,
    Pending,
    Quarantined
};
struct RHIDescriptorVersionHandle
{
    uint32_t slot{ UINT32_MAX };
    uint32_t generation{ 0 };

    bool IsValid() const { return UINT32_MAX != slot && 0 != generation; }

    uint64_t ToToken() const
    {
        if (!IsValid()) return 0;
        return (static_cast<uint64_t>(generation) << 32) |
            static_cast<uint64_t>(slot + 1);
    }

    static RHIDescriptorVersionHandle FromToken(uint64_t token)
    {
        if (0 == token) return {};
        const uint32_t encodedSlot = static_cast<uint32_t>(token & 0xffffffffull);
        if (0 == encodedSlot) return {};
        return RHIDescriptorVersionHandle{
            encodedSlot - 1, static_cast<uint32_t>(token >> 32) };
    }
};

struct RHIDescriptorVersionAcquire
{
    RHIDescriptorVersionHandle handle{};
    bool grew{ false };

    bool IsValid() const { return handle.IsValid(); }
};

struct RHIDescriptorVersionStats
{
    uint32_t versions{ 0 };
    uint32_t available{ 0 };
    uint32_t recording{ 0 };
    uint32_t pending{ 0 };
    uint32_t quarantined{ 0 };
    uint32_t peakVersions{ 0 };
    uint64_t creates{ 0 };
    uint64_t reuses{ 0 };
    uint64_t submissions{ 0 };
    uint64_t collections{ 0 };
    uint64_t aborts{ 0 };
    uint64_t invalidTransitions{ 0 };
    uint64_t oldestPendingValue{ 0 };
};

class RHIDescriptorVersionPolicy
{
public:
    void Reset(uint32_t initialVersions = 0)
    {
        m_slots.clear();
        m_slots.resize(initialVersions);
        for (Slot& slot : m_slots) slot.generation = 1;
        m_creates = initialVersions;
        m_reuses = 0;
        m_submissions = 0;
        m_collections = 0;
        m_aborts = 0;
        m_invalidTransitions = 0;
        m_peakVersions = initialVersions;
    }

    RHIDescriptorVersionAcquire BeginRecording(uint64_t recordingId)
    {
        if (0 == recordingId)
        {
            ++m_invalidTransitions;
            return {};
        }

        // BeginRecording은 coordinator 복구 경로에서 같은 id로 두 번 불려도
        // 같은 버전을 돌려주는 멱등 연산이다.
        for (uint32_t i = 0; i < m_slots.size(); ++i)
        {
            Slot& slot = m_slots[i];
            if (slot.state == RHIDescriptorVersionState::Recording &&
                slot.recordingId == recordingId)
            {
                return { RHIDescriptorVersionHandle{ i, slot.generation }, false };
            }
            if (slot.state != RHIDescriptorVersionState::Available &&
                slot.recordingId == recordingId)
            {
                ++m_invalidTransitions;
                return {};
            }
        }

        for (uint32_t i = 0; i < m_slots.size(); ++i)
        {
            Slot& slot = m_slots[i];
            if (slot.state != RHIDescriptorVersionState::Available) continue;

            if (slot.everUsed)
            {
                ++slot.generation;
                if (0 == slot.generation) ++slot.generation;
                ++m_reuses;
            }
            slot.everUsed = true;
            slot.state = RHIDescriptorVersionState::Recording;
            slot.recordingId = recordingId;
            slot.completionValue = 0;
            return { RHIDescriptorVersionHandle{ i, slot.generation }, false };
        }

        if (m_slots.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
        {
            ++m_invalidTransitions;
            return {};
        }

        Slot slot{};
        slot.state = RHIDescriptorVersionState::Recording;
        slot.recordingId = recordingId;
        slot.generation = 1;
        slot.everUsed = true;
        m_slots.push_back(slot);
        ++m_creates;
        if (m_slots.size() > m_peakVersions)
            m_peakVersions = static_cast<uint32_t>(m_slots.size());
        return { RHIDescriptorVersionHandle{
            static_cast<uint32_t>(m_slots.size() - 1), 1 }, true };
    }

    bool OnSubmitted(uint64_t recordingId, RHICompletionPoint completion)
    {
        Slot* const slot = FindRecording(recordingId);
        if (nullptr == slot)
        {
            ++m_invalidTransitions;
            return false;
        }

        slot->state = completion.IsValid()
            ? RHIDescriptorVersionState::Pending
            : RHIDescriptorVersionState::Quarantined;
        slot->completionValue = completion.value;
        ++m_submissions;
        return true;
    }

    bool AbortRecording(uint64_t recordingId)
    {
        Slot* const slot = FindRecording(recordingId);
        if (nullptr == slot)
        {
            ++m_invalidTransitions;
            return false;
        }

        slot->state = RHIDescriptorVersionState::Available;
        slot->recordingId = 0;
        slot->completionValue = 0;
        ++m_aborts;
        return true;
    }

    uint32_t Collect(RHICompletionPoint completed)
    {
        uint32_t count = 0;
        for (Slot& slot : m_slots)
        {
            if (slot.state != RHIDescriptorVersionState::Pending ||
                0 == slot.completionValue || completed.value < slot.completionValue)
            {
                continue;
            }

            slot.state = RHIDescriptorVersionState::Available;
            slot.recordingId = 0;
            slot.completionValue = 0;
            ++count;
        }
        m_collections += count;
        return count;
    }

    bool IsCurrent(RHIDescriptorVersionHandle handle) const
    {
        if (!handle.IsValid() || handle.slot >= m_slots.size()) return false;
        const Slot& slot = m_slots[handle.slot];
        return slot.state == RHIDescriptorVersionState::Recording &&
            slot.generation == handle.generation;
    }

    RHIDescriptorVersionStats GetStats() const
    {
        RHIDescriptorVersionStats stats{};
        stats.versions = static_cast<uint32_t>(m_slots.size());
        stats.peakVersions = m_peakVersions;
        stats.creates = m_creates;
        stats.reuses = m_reuses;
        stats.submissions = m_submissions;
        stats.collections = m_collections;
        stats.aborts = m_aborts;
        stats.invalidTransitions = m_invalidTransitions;

        for (const Slot& slot : m_slots)
        {
            switch (slot.state)
            {
            case RHIDescriptorVersionState::Available: ++stats.available; break;
            case RHIDescriptorVersionState::Recording: ++stats.recording; break;
            case RHIDescriptorVersionState::Pending:
                ++stats.pending;
                if (0 == stats.oldestPendingValue ||
                    slot.completionValue < stats.oldestPendingValue)
                    stats.oldestPendingValue = slot.completionValue;
                break;
            case RHIDescriptorVersionState::Quarantined: ++stats.quarantined; break;
            }
        }
        return stats;
    }

private:
    struct Slot
    {
        RHIDescriptorVersionState state{ RHIDescriptorVersionState::Available };
        uint64_t recordingId{ 0 };
        uint64_t completionValue{ 0 };
        uint32_t generation{ 0 };
        bool everUsed{ false };
    };

    Slot* FindRecording(uint64_t recordingId)
    {
        if (0 == recordingId) return nullptr;
        for (Slot& slot : m_slots)
        {
            if (slot.state == RHIDescriptorVersionState::Recording &&
                slot.recordingId == recordingId)
                return &slot;
        }
        return nullptr;
    }

    std::vector<Slot> m_slots;
    uint32_t m_peakVersions{ 0 };
    uint64_t m_creates{ 0 };
    uint64_t m_reuses{ 0 };
    uint64_t m_submissions{ 0 };
    uint64_t m_collections{ 0 };
    uint64_t m_aborts{ 0 };
    uint64_t m_invalidTransitions{ 0 };
};
