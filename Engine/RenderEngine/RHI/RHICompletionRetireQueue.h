#include "../../Containers/ContainerAPI.h"
#pragma once

#include "RHIResourceTypes.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

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
        size_t write = 0;
        for (size_t read = 0; read < m_entries.size(); ++read)
        {
            Entry& entry = m_entries[read];
            if (!RHICompletionRetirePolicy::CanCollect(
                    completed.value, entry.completion.value))
            {
                if (write != read) m_entries[write] = std::move(entry);
                ++write;
                continue;
            }

            try
            {
                release(entry.payload);
            }
            catch (...)
            {
                // Earlier collections remain committed, but the throwing entry
                // and unvisited tail must remain queued, as with repeated erase.
                for (size_t tail = read; tail < m_entries.size(); ++tail, ++write)
                    if (write != tail) m_entries[write] = std::move(m_entries[tail]);
                m_entries.erase(m_entries.begin() + write, m_entries.end());
                throw;
            }
            ++result.count;
            result.bytes += entry.bytes;
            m_pendingBytes -= entry.bytes;
            // Release residual RAII ownership before the next callback, rather
            // than retaining collected resources until the final tail erase.
            // The callback may also have transferred ownership elsewhere.
            [[maybe_unused]] Payload retired(std::move(entry.payload));
        }
        // Stable in-place compaction: no allocation, no survivor self-move,
        // at most one move assignment per survivor, and capacity is retained.
        m_entries.erase(m_entries.begin() + write, m_entries.end());
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

    ce::dynamic_array<Entry> m_entries;
    uint64_t m_pendingBytes{ 0 };
    size_t m_quarantinedCount{ 0 };
};
