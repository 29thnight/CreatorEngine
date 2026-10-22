#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>

namespace ce
{
    class stack_arena;

    // Borrowed, bounded CPU scratch storage. One writer owns the cursor; there is
    // no heap fallback, synchronization, or object destruction. The backing span
    // must outlive its allocations. Destroy objects before reset or owner teardown.
    class linear_arena final
    {
      public:
        linear_arena() noexcept = default;
        explicit linear_arena(std::span<std::byte> storage) noexcept : m_storage(storage) {}
        linear_arena(const linear_arena&) = delete;
        linear_arena& operator=(const linear_arena&) = delete;
        linear_arena(linear_arena&&) = delete;
        linear_arena& operator=(linear_arena&&) = delete;

        // Zero-byte requests consume one byte. Invalid alignment and exhaustion
        // return null without changing the cursor or successful-allocation count.
        [[nodiscard]] void* try_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept
        {
            if (!alignment || (alignment & (alignment - 1)))
            {
                return nullptr;
            }
            bytes = bytes ? bytes : 1;
            const std::size_t available = remaining_bytes();
            if (bytes > available)
            {
                return nullptr;
            }
            std::byte* begin = m_storage.data() + m_usedBytes;
            const std::size_t padding = (std::uintptr_t{} - reinterpret_cast<std::uintptr_t>(begin)) & (alignment - 1);
            if (padding > available || bytes > available - padding)
            {
                return nullptr;
            }
            m_usedBytes += padding + bytes;
            if (m_peakBytes < m_usedBytes)
            {
                m_peakBytes = m_usedBytes;
            }
            ++m_allocations;
            return begin + padding;
        }

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
        {
            void* memory = try_allocate(bytes, alignment);
            if (!memory)
            {
                throw std::bad_alloc();
            }
            return memory;
        }

        // Reuses the same backing bytes; high-water and allocation totals survive.
        void reset() noexcept
        {
            m_usedBytes = 0;
            ++m_resets;
        }

        std::size_t capacity() const noexcept { return m_storage.size(); }
        std::size_t used_bytes() const noexcept { return m_usedBytes; }
        std::size_t remaining_bytes() const noexcept { return capacity() - m_usedBytes; }
        std::size_t peak_bytes() const noexcept { return m_peakBytes; }
        std::uint64_t allocation_count() const noexcept { return m_allocations; }
        std::uint64_t reset_count() const noexcept { return m_resets; }

      private:
        friend class stack_arena;
        void rewind_to(std::size_t usedBytes) noexcept { m_usedBytes = usedBytes; }

        std::span<std::byte> m_storage;
        std::size_t m_usedBytes{};
        std::size_t m_peakBytes{};
        std::uint64_t m_allocations{};
        std::uint64_t m_resets{};
    };
} // namespace ce
