#pragma once
#include "LinearArena.h"
#include <exception>
#include <stdexcept>

namespace ce
{
    // Bounded scratch storage with nested, address-bound RAII checkpoints. A
    // checkpoint cannot be copied, moved, forged from an offset, or reused after
    // rewind. Explicit out-of-order/foreign rewinds fail without changing state.
    // The backing buffer outlives the arena; every scope must die before it.
    // These scopes manage raw bytes only: destroy objects before scope rollback.
    class stack_arena final
    {
      public:
        class scope final
        {
          public:
            scope(const scope&) = delete;
            scope& operator=(const scope&) = delete;
            scope(scope&&) = delete;
            scope& operator=(scope&&) = delete;

            ~scope() noexcept
            {
                if (m_owner && !m_owner->rewind(*this))
                {
                    // Letting an outer scope die while an inner one still owns
                    // its checkpoint would leave a dangling checkpoint chain.
                    std::terminate();
                }
            }

            bool active() const noexcept { return m_owner != nullptr; }
            bool rewind() noexcept { return m_owner && m_owner->rewind(*this); }

          private:
            friend class stack_arena;
            explicit scope(stack_arena& owner) noexcept
                : m_owner(&owner), m_previous(owner.m_scope), m_usedBytes(owner.used_bytes())
            {
                owner.m_scope = this;
            }

            stack_arena* m_owner;
            scope* m_previous;
            std::size_t m_usedBytes;
        };

        stack_arena() noexcept = default;
        explicit stack_arena(std::span<std::byte> storage) noexcept : m_arena(storage) {}
        stack_arena(const stack_arena&) = delete;
        stack_arena& operator=(const stack_arena&) = delete;
        stack_arena(stack_arena&&) = delete;
        stack_arena& operator=(stack_arena&&) = delete;

        ~stack_arena() noexcept
        {
            if (m_scope)
            {
                std::terminate();
            }
        }

        [[nodiscard]] scope checkpoint() noexcept { return scope{*this}; }

        bool rewind(scope& checkpoint) noexcept
        {
            if (checkpoint.m_owner != this || m_scope != &checkpoint)
            {
                return false;
            }
            m_arena.rewind_to(checkpoint.m_usedBytes);
            m_scope = checkpoint.m_previous;
            checkpoint.m_owner = nullptr;
            checkpoint.m_previous = nullptr;
            ++m_rewinds;
            return true;
        }

        [[nodiscard]] void* try_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept
        {
            return m_arena.try_allocate(bytes, alignment);
        }

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
        {
            return m_arena.allocate(bytes, alignment);
        }

        bool try_reset() noexcept
        {
            if (m_scope)
            {
                return false;
            }
            m_arena.reset();
            return true;
        }

        void reset()
        {
            if (!try_reset())
            {
                throw std::logic_error("Stack arena cannot reset while a checkpoint is active.");
            }
        }

        std::size_t capacity() const noexcept { return m_arena.capacity(); }
        std::size_t used_bytes() const noexcept { return m_arena.used_bytes(); }
        std::size_t remaining_bytes() const noexcept { return m_arena.remaining_bytes(); }
        std::size_t peak_bytes() const noexcept { return m_arena.peak_bytes(); }
        std::uint64_t allocation_count() const noexcept { return m_arena.allocation_count(); }
        std::uint64_t reset_count() const noexcept { return m_arena.reset_count(); }
        std::uint64_t rewind_count() const noexcept { return m_rewinds; }

      private:
        linear_arena m_arena;
        scope* m_scope{};
        std::uint64_t m_rewinds{};
    };
} // namespace ce
