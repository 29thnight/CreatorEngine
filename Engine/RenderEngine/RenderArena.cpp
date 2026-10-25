#include "RenderArena.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Arena.h"
#else
import Creator.Core.Arena;
#endif
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace ce
{
    render_arena_resource& RenderArenaSet::Resource(size_t domain)
    {
        if (domain >= DomainCount)
        {
            throw std::out_of_range("Render arena writer domain is out of range.");
        }
        return m_resources[domain];
    }

    size_t RenderArenaSet::RetainedBytes() const noexcept
    {
        size_t bytes{};
        for (const auto& resource : m_resources)
        {
            bytes += resource.stats().retained_bytes;
        }
        return bytes;
    }

    void RenderArenaSet::Reset() noexcept
    {
        for (auto& resource : m_resources)
        {
            resource.reset();
        }
    }

    struct RenderArenaPool::State
    {
        static constexpr size_t MaxFreeSets = 8;
        static constexpr size_t MaxFreeBytes = 16 * 1024 * 1024;
        static_assert(std::atomic<RenderArenaSet*>::is_always_lock_free);
        static_assert(std::atomic<size_t>::is_always_lock_free);
        // Each exchange owns the entire set. No intrusive next pointer is read
        // after publication, so removal/republication cannot create an ABA link.
        std::array<std::atomic<RenderArenaSet*>, MaxFreeSets> free{};
        std::atomic<size_t> bytes{};

        ~State()
        {
            for (auto& slot : free)
            {
                render_delete<RenderArenaSet>{}(slot.load(std::memory_order_relaxed));
            }
        }

        void Release(RenderArenaSet* set) noexcept
        {
            set->Reset();
            const auto retained = set->RetainedBytes();
            auto used = bytes.load(std::memory_order_relaxed);
            while (retained <= MaxFreeBytes && used <= MaxFreeBytes - retained)
            {
                if (bytes.compare_exchange_weak(used, used + retained, std::memory_order_relaxed))
                {
                    for (auto& slot : free)
                    {
                        RenderArenaSet* empty{};
                        if (slot.compare_exchange_strong(empty, set, std::memory_order_release,
                                                         std::memory_order_relaxed))
                        {
                            return;
                        }
                    }
                    bytes.fetch_sub(retained, std::memory_order_relaxed);
                    break;
                }
            }
            render_delete<RenderArenaSet>{}(set);
        }
    };

    RenderArenaPool::RenderArenaPool() : m_state(make_render_shared<State>()) {}

    RenderArenaLease RenderArenaPool::Acquire() const
    {
        RenderArenaSet* set{};
        for (auto& slot : m_state->free)
        {
            set = slot.exchange(nullptr, std::memory_order_acquire);
            if (set)
            {
                m_state->bytes.fetch_sub(set->RetainedBytes(), std::memory_order_relaxed);
                break;
            }
        }
        if (!set)
        {
            auto* memory = render_allocator<RenderArenaSet>{}.allocate(1);
            try
            {
                set = std::construct_at(memory);
            }
            catch (...)
            {
                render_allocator<RenderArenaSet>{}.deallocate(memory, 1);
                throw;
            }
        }
        return RenderArenaLease(
            set, [state = m_state](RenderArenaSet* value) { state->Release(value); }, render_allocator<std::byte>{});
    }
} // namespace ce
