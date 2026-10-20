#pragma once
#include "RenderAllocator.h"
#include <array>
#include <algorithm>
#include <span>
#include <stdexcept>
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

    // Reusable declaration-local identity set. Reset reserves once and clears
    // flat pointer slots; insertion neither allocates nodes nor takes a lock.
    template<class T>
    class render_pointer_set final
    {
      public:
        void reset(size_t maximum)
        {
            if (maximum > (std::numeric_limits<size_t>::max)() / 2)
            {
                throw std::bad_array_new_length();
            }
            size_t slots = 8;
            while (slots < maximum * 2)
            {
                if (slots > (std::numeric_limits<size_t>::max)() / 2)
                {
                    throw std::bad_array_new_length();
                }
                slots *= 2;
            }
            if (slots_.size() < slots)
            {
                slots_.resize(slots);
            }
            std::fill_n(slots_.data(), slots_.size(), nullptr);
            maximum_ = maximum;
            size_ = 0;
        }
        bool insert(const T* value)
        {
            if (!value || slots_.empty())
            {
                throw std::invalid_argument("Pointer identity set requires a nonnull value and reset capacity.");
            }
            auto hash = reinterpret_cast<uintptr_t>(value) >> 3;
            hash ^= hash >> 17;
            hash *= uintptr_t{0x9e3779b1};
            size_t index = hash & (slots_.size() - 1);
            while (const auto* occupied = slots_[index])
            {
                if (occupied == value)
                {
                    return false;
                }
                index = (index + 1) & (slots_.size() - 1);
            }
            if (size_ == maximum_)
            {
                throw std::length_error("Pointer identity set exceeded its reset capacity.");
            }
            slots_[index] = value;
            ++size_;
            return true;
        }
        void clear() noexcept
        {
            std::fill_n(slots_.data(), slots_.size(), nullptr);
            size_ = 0;
        }

      private:
        render_vector<const T*> slots_;
        size_t maximum_{};
        size_t size_{};
    };

    // Single-writer objects with stable addresses and reverse destruction order.
    // Reset destroys captures before rewinding the retained non-pmr blocks.
    class render_arena_objects final
    {
      public:
        ~render_arena_objects() { reset(); }
        render_arena_objects() = default;
        render_arena_objects(const render_arena_objects&) = delete;
        render_arena_objects& operator=(const render_arena_objects&) = delete;

        template<class T, class... Args>
        T* emplace(Args&&... args)
        {
            static_assert(std::is_nothrow_destructible_v<T>);
            auto* record = static_cast<destructor_record*>(
                resource_.allocate(sizeof(destructor_record), alignof(destructor_record)));
            auto* memory = static_cast<T*>(resource_.allocate(sizeof(T), alignof(T)));
            // A throwing constructor never publishes a destructor record.
            auto* value = std::construct_at(memory, std::forward<Args>(args)...);
            std::construct_at(record, destructor_record{head_, value, [](void* object) noexcept
                                                        { std::destroy_at(static_cast<T*>(object)); }});
            head_ = record;
            return value;
        }

        template<class T>
        std::span<const T> copy(std::span<const T> values)
        {
            if (values.empty())
            {
                return {};
            }
            struct array_owner
            {
                T* data;
                size_t size{};
                ~array_owner()
                {
                    while (size)
                    {
                        std::destroy_at(data + --size);
                    }
                }
            };
            if (values.size() > (std::numeric_limits<size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            auto* data = static_cast<T*>(resource_.allocate(values.size_bytes(), alignof(T)));
            auto* owner = emplace<array_owner>(data);
            try
            {
                for (const auto& value : values)
                {
                    std::construct_at(data + owner->size, value);
                    ++owner->size;
                }
            }
            catch (...)
            {
                while (owner->size)
                {
                    std::destroy_at(data + --owner->size);
                }
                throw;
            }
            return {data, owner->size};
        }

        void reset() noexcept
        {
            while (head_)
            {
                auto* record = head_;
                head_ = record->previous;
                record->destroy(record->value);
            }
            resource_.reset();
        }
        render_arena_stats stats() const noexcept { return resource_.stats(); }

      private:
        struct destructor_record
        {
            destructor_record* previous;
            void* value;
            void (*destroy)(void*) noexcept;
        };
        render_arena_resource resource_;
        destructor_record* head_{};
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
