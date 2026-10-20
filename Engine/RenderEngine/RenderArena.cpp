#include "RenderArena.h"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace ce
{
    render_arena_resource::~render_arena_resource()
    {
        while (head_)
        {
            auto* next = head_->next;
            mi_free(head_);
            head_ = next;
        }
    }

    void render_arena_resource::reset() noexcept
    {
        for (auto* entry = head_; entry; entry = entry->next)
        {
            entry->used = 0;
        }
        current_ = head_;
        stats_.activeBlocks = 0;
        stats_.activeCapacity = 0;
        stats_.usedBytes = 0;
        ++stats_.resets;
    }

    void* render_arena_resource::allocate(size_t bytes, size_t alignment)
    {
        if (!alignment || (alignment & (alignment - 1)) || bytes > SIZE_MAX - alignment - sizeof(block))
        {
            throw std::bad_alloc();
        }
        bytes = (std::max)(bytes, size_t{1});
        void* memory{};
        if (try_allocate(bytes, alignment, memory))
        {
            return memory;
        }
        auto* entry = current_;
        block* previous{};
        while (entry)
        {
            auto* begin = entry->data() + entry->used;
            size_t available = entry->capacity - entry->used;
            void* aligned = begin;
            if (std::align(alignment, bytes, aligned, available))
            {
                if (!entry->used)
                {
                    ++stats_.activeBlocks;
                    stats_.activeCapacity += entry->capacity;
                }
                entry->used = static_cast<std::byte*>(aligned) - entry->data() + bytes;
                stats_.usedBytes += bytes;
                current_ = entry;
                return aligned;
            }
            previous = entry;
            entry = entry->next;
        }
        const size_t capacity = (std::max)(size_t{65536}, bytes + alignment - 1);
        memory = mi_malloc(sizeof(block) + capacity);
        if (!memory)
        {
            throw std::bad_alloc();
        }
        entry = new (memory) block{nullptr, capacity, 0};
        if (previous)
        {
            previous->next = entry;
        }
        else if (!head_)
        {
            head_ = entry;
        }
        else
        {
            // current_ is null only for a fresh resource.
            mi_free(entry);
            throw std::logic_error("Render arena lost its retained block cursor.");
        }
        current_ = entry;
        ++stats_.blockCount;
        stats_.retainedBytes += capacity;
        return allocate(bytes, alignment);
    }

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
            bytes += resource.stats().retainedBytes;
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
            set = make_render_unique<RenderArenaSet>().release();
        }
        return RenderArenaLease(
            set, [state = m_state](RenderArenaSet* value) { state->Release(value); }, render_allocator<std::byte>{});
    }
} // namespace ce
