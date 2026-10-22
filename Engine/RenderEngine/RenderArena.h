#pragma once
#include "../Utility_Framework/Arena.h"
#include "../Utility_Framework/Ownership.h"
#include "RenderAllocator.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <stdexcept>

namespace ce
{
    // Compatibility names keep renderer call sites bound to the common
    // implementation; render-specific ownership and pool policy stay here.
    using render_arena_stats = arena_stats;
    using render_arena_resource = arena_resource;
    using render_arena_objects = arena_objects;
    template<class T>
    using render_arena_buffer = arena_buffer<T>;
    template<typename T>
    using render_arena_allocator = arena_allocator<T>;
    template<typename T>
    using render_arena_vector = arena_vector<T>;
    template<typename Key, typename Value, typename Compare = std::less<Key>>
    using render_arena_map = arena_map<Key, Value, Compare>;

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

    using RenderArenaLease = pooled_owner<RenderArenaSet>;

    // Last lease release resets the set. Pending CPU/RHI/GPU owners exclude reuse.
    // Shared state outlives the pool facade when a retirement still owns a lease.
    class RenderArenaPool final
    {
      public:
        RenderArenaPool();
        RenderArenaLease Acquire() const;

      private:
        struct State;
        own::shared_owner<State> m_state;
    };
} // namespace ce
