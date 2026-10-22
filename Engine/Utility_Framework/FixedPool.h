#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace ce
{
    // Single-writer, fixed-capacity object storage. The free list occupies unused
    // object slots; a compact live bitmap validates releases and drives teardown.
    // emplace/destroy are O(1); construction/clear are O(Capacity). The pool itself
    // never allocates. T's constructor/destructor must not reenter this same pool.
    // Returned pointers borrow the pool and become invalid immediately on destroy.
    // As with ordinary raw pointers, same-address reuse does not detect a stale
    // pointer from an earlier occupant; callers needing that require generation handles.
    template<class T, std::size_t Capacity>
    class fixed_pool final
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>);
        static_assert(Capacity > 0);

        union slot
        {
            T value;
            std::size_t next;
            slot() noexcept : next(Capacity) {}
            ~slot() noexcept {}
        };
        static_assert(Capacity <= (std::numeric_limits<std::size_t>::max)() / sizeof(slot));
        static constexpr std::size_t BitsPerWord = 64;
        static constexpr std::size_t WordCount = Capacity / BitsPerWord + (Capacity % BitsPerWord != 0);

      public:
        fixed_pool() noexcept
        {
            for (std::size_t index = 0; index < Capacity; ++index)
            {
                m_slots[index].next = index + 1;
            }
        }
        ~fixed_pool() noexcept { clear(); }
        fixed_pool(const fixed_pool&) = delete;
        fixed_pool& operator=(const fixed_pool&) = delete;
        fixed_pool(fixed_pool&&) = delete;
        fixed_pool& operator=(fixed_pool&&) = delete;

        // A full pool returns null without constructing T. Exceptions from T's
        // constructor propagate after the reserved slot is returned to the list.
        template<class... Args>
        [[nodiscard]] T* try_emplace(Args&&... args)
        {
            if (m_free == Capacity)
            {
                return nullptr;
            }
            const std::size_t index = m_free;
            slot& entry = m_slots[index];
            m_free = entry.next;
            try
            {
                T* value = std::construct_at(std::addressof(entry.value), std::forward<Args>(args)...);
                m_live[index / BitsPerWord] |= std::uint64_t{1} << (index % BitsPerWord);
                ++m_size;
                return value;
            }
            catch (...)
            {
                std::construct_at(std::addressof(entry.next), m_free);
                m_free = index;
                throw;
            }
        }

        template<class... Args>
        [[nodiscard]] T* emplace(Args&&... args)
        {
            T* value = try_emplace(std::forward<Args>(args)...);
            if (!value)
            {
                throw std::bad_alloc();
            }
            return value;
        }

        // Only exact, currently occupied slot addresses are accepted. No
        // subtraction or ordering of unrelated object pointers is performed.
        bool destroy(T* value) noexcept
        {
            const std::size_t index = live_index(value);
            if (index == Capacity)
            {
                return false;
            }
            destroy_slot(index);
            return true;
        }

        void clear() noexcept
        {
            for (std::size_t index = 0; index < Capacity; ++index)
            {
                if (is_live(index))
                {
                    destroy_slot(index);
                }
            }
        }

        bool owns(const T* value) const noexcept { return live_index(value) != Capacity; }
        static constexpr std::size_t capacity() noexcept { return Capacity; }
        std::size_t size() const noexcept { return m_size; }
        std::size_t available() const noexcept { return Capacity - m_size; }
        bool empty() const noexcept { return m_size == 0; }
        bool full() const noexcept { return m_size == Capacity; }

      private:
        bool is_live(std::size_t index) const noexcept
        {
            return (m_live[index / BitsPerWord] & (std::uint64_t{1} << (index % BitsPerWord))) != 0;
        }

        std::size_t live_index(const T* value) const noexcept
        {
            if (!value)
            {
                return Capacity;
            }
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
            const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(m_slots.data());
            if (address < first)
            {
                return Capacity;
            }
            const std::uintptr_t offset = address - first;
            if (offset % sizeof(slot))
            {
                return Capacity;
            }
            const std::uintptr_t index = offset / sizeof(slot);
            if (index >= Capacity || !is_live(static_cast<std::size_t>(index)))
            {
                return Capacity;
            }
            return static_cast<std::size_t>(index);
        }

        void destroy_slot(std::size_t index) noexcept
        {
            m_live[index / BitsPerWord] &= ~(std::uint64_t{1} << (index % BitsPerWord));
            --m_size;
            slot& entry = m_slots[index];
            std::destroy_at(std::addressof(entry.value));
            std::construct_at(std::addressof(entry.next), m_free);
            m_free = index;
        }

        std::array<slot, Capacity> m_slots;
        std::array<std::uint64_t, WordCount> m_live{};
        std::size_t m_free{};
        std::size_t m_size{};
    };
} // namespace ce
