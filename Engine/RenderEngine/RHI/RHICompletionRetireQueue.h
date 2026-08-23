#pragma once

#include "RHIResourceTypes.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// graphics queue의 completion point가 지나기 전까지 payload를 보관하는 공통 큐.
//
// Payload는 DX12 ComPtr 묶음, Vulkan RHI handle 묶음, descriptor record처럼
// backend마다 달라도 된다. 공통 계층은 네이티브 타입이나 파괴 방법을 모르며,
// 호출자가 Collect/Drain에 넘긴 releaser만 실행한다.
//
// completion value 0은 제출 완료를 증명하지 못한 quarantine이다. Collect로는
// 절대 회수되지 않고 device teardown의 Drain에서만 파괴된다.
struct RHIRetireCollection
{
    size_t count{ 0 };
    uint64_t bytes{ 0 };
};

struct RHICompletionRetirePolicy
{
    static constexpr bool CanCollect(uint64_t completedValue,
        uint64_t retireValue)
    {
        return 0 != retireValue && completedValue >= retireValue;
    }
};

static_assert(!RHICompletionRetirePolicy::CanCollect(6, 7));
static_assert(RHICompletionRetirePolicy::CanCollect(7, 7));
static_assert(!RHICompletionRetirePolicy::CanCollect(~uint64_t{ 0 }, 0));

template <typename Payload>
class RHICompletionRetireQueue
{
public:
    RHICompletionRetireQueue() = default;
    RHICompletionRetireQueue(const RHICompletionRetireQueue&) = delete;
    RHICompletionRetireQueue& operator=(const RHICompletionRetireQueue&) = delete;
    RHICompletionRetireQueue(RHICompletionRetireQueue&&) = delete;
    RHICompletionRetireQueue& operator=(RHICompletionRetireQueue&&) = delete;

    void Enqueue(RHICompletionPoint completion, Payload payload,
        uint64_t bytes = 0)
    {
        m_entries.push_back(Entry{ completion, bytes, std::move(payload) });
        m_pendingBytes += bytes;
        if (!completion.IsValid()) ++m_quarantinedCount;
    }

    template <typename Releaser>
    RHIRetireCollection Collect(RHICompletionPoint completed, Releaser&& release)
    {
        RHIRetireCollection result{};
        auto it = m_entries.begin();
        while (it != m_entries.end())
        {
            if (!RHICompletionRetirePolicy::CanCollect(
                    completed.value, it->completion.value))
            {
                ++it;
                continue;
            }

            release(it->payload);
            ++result.count;
            result.bytes += it->bytes;
            m_pendingBytes -= it->bytes;
            it = m_entries.erase(it);
        }
        return result;
    }

    // Device teardown처럼 모든 producer/consumer가 멈춘 뒤에만 부른다.
    // completion을 증명하지 못한 quarantine도 이 경로에서 함께 파괴한다.
    template <typename Releaser>
    RHIRetireCollection Drain(Releaser&& release)
    {
        RHIRetireCollection result{};
        result.count = m_entries.size();
        result.bytes = m_pendingBytes;
        for (Entry& entry : m_entries) release(entry.payload);
        m_entries.clear();
        m_pendingBytes = 0;
        m_quarantinedCount = 0;
        return result;
    }

    size_t GetPendingCount() const { return m_entries.size(); }
    uint64_t GetPendingBytes() const { return m_pendingBytes; }
    size_t GetQuarantinedCount() const { return m_quarantinedCount; }
    bool Empty() const { return m_entries.empty(); }

private:
    struct Entry
    {
        RHICompletionPoint completion;
        uint64_t bytes{ 0 };
        Payload payload;
    };

    std::vector<Entry> m_entries;
    uint64_t m_pendingBytes{ 0 };
    size_t m_quarantinedCount{ 0 };
};
