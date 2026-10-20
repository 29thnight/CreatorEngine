#pragma once
#include "RenderAllocator.h"
#include <array>
#include <map>
#include <tuple>

namespace ce
{
    struct render_arena_stats
    {
        size_t retainedBytes{};
        size_t blockCount{};
        size_t activeBlocks{};
        size_t activeCapacity{};
        size_t usedBytes{};
        uint64_t resets{};
    };

    // One writer per resource. Owners destroy every object/container before reset.
    // Raw blocks stay at stable addresses until this resource is destroyed.
    class render_arena_resource final
    {
      public:
        render_arena_resource() = default;
        ~render_arena_resource();
        render_arena_resource(const render_arena_resource&) = delete;
        render_arena_resource& operator=(const render_arena_resource&) = delete;
        [[nodiscard]] void* allocate(size_t bytes, size_t alignment);
        bool try_allocate(size_t bytes, size_t alignment, void*& memory) noexcept
        {
            if (!current_ || !current_->used || !bytes || !alignment || (alignment & (alignment - 1)))
            {
                return false;
            }
            auto* begin = current_->data() + current_->used;
            const size_t padding = (uintptr_t{} - reinterpret_cast<uintptr_t>(begin)) & (alignment - 1);
            const size_t available = current_->capacity - current_->used;
            if (padding > available || bytes > available - padding)
            {
                return false;
            }
            memory = begin + padding;
            current_->used += padding + bytes;
            stats_.usedBytes += bytes;
            return true;
        }
        void reset() noexcept;
        const render_arena_stats& stats() const noexcept { return stats_; }

      private:
        struct block
        {
            block* next{};
            size_t capacity{};
            size_t used{};
            std::byte* data() noexcept { return reinterpret_cast<std::byte*>(this + 1); }
        };
        block* head_{};
        block* current_{};
        render_arena_stats stats_;
    };

    // Stateful STL allocator without a virtual memory_resource dispatch. Nested
    // containers and map values receive the same writer domain through construct.
    template<typename T>
    class render_arena_allocator
    {
      public:
        using value_type = T;
        using is_always_equal = std::false_type;

        render_arena_allocator() noexcept = default;
        render_arena_allocator(render_arena_resource* resource) noexcept : resource_(resource) {}
        template<typename U>
        render_arena_allocator(const render_arena_allocator<U>& other) noexcept : resource_(other.resource())
        {
        }

        render_arena_resource* resource() const noexcept { return resource_; }
        // A plain container copy may outlive the source frame. Only explicitly
        // allocator-bound construction is allowed to borrow the source domain.
        render_arena_allocator select_on_container_copy_construction() const noexcept { return {}; }

        [[nodiscard]] T* allocate(size_t count)
        {
            if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            return resource_ ? static_cast<T*>(resource_->allocate(count * sizeof(T), alignof(T)))
                             : render_allocator<T>{}.allocate(count);
        }

        void deallocate(T* memory, size_t count) noexcept
        {
            if (!resource_)
            {
                render_allocator<T>{}.deallocate(memory, count);
            }
        }

        template<typename U, typename... Args>
        void construct(U* memory, Args&&... args)
        {
            std::apply([memory](auto&&... values)
                       { std::construct_at(memory, std::forward<decltype(values)>(values)...); },
                       std::uses_allocator_construction_args<U>(*this, std::forward<Args>(args)...));
        }

        template<typename U>
        bool operator==(const render_arena_allocator<U>& other) const noexcept
        {
            return resource_ == other.resource();
        }

      private:
        render_arena_resource* resource_{};
    };

    template<typename T>
    using render_arena_vector = std::vector<T, render_arena_allocator<T>>;

    template<typename Key, typename Value, typename Compare = std::less<Key>>
    using render_arena_map = std::map<Key, Value, Compare, render_arena_allocator<std::pair<const Key, Value>>>;

    // Independent writers use separate domains; no lock is taken by allocation.
    class RenderArenaSet final
    {
      public:
        static constexpr size_t DomainCount = 4;
        render_arena_resource& Resource(size_t domain);
        size_t RetainedBytes() const noexcept;
        void Reset() noexcept;

      private:
        std::array<render_arena_resource, DomainCount> m_resources;
    };

    using RenderArenaLease = std::shared_ptr<RenderArenaSet>;

    // Last lease release resets the set. Pending CPU/RHI/GPU owners exclude reuse.
    // Shared state outlives the pool facade when a retirement still owns a lease.
    class RenderArenaPool final
    {
      public:
        RenderArenaPool();
        RenderArenaLease Acquire() const;

      private:
        struct State;
        std::shared_ptr<State> m_state;
    };
} // namespace ce
