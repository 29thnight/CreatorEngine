export module Creator.Core.BoundedMemory;
export import "Creator.Containers.h";
import std;

export namespace ce
{
    class stack_arena;

    // Borrowed, bounded CPU scratch storage. One writer owns the cursor; there is
    // no heap fallback, synchronization, or object destruction. The backing span
    // must outlive its allocations. Destroy objects before reset or owner teardown.
    class linear_arena final
    {
      public:
        linear_arena() noexcept = default;
        explicit linear_arena(std::span<std::byte> storage) noexcept : storage_(storage) {}
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
            std::byte* begin = storage_.data() + used_bytes_;
            const std::size_t padding = (std::uintptr_t{} - reinterpret_cast<std::uintptr_t>(begin)) & (alignment - 1);
            if (padding > available || bytes > available - padding)
            {
                return nullptr;
            }
            used_bytes_ += padding + bytes;
            if (peak_bytes_ < used_bytes_)
            {
                peak_bytes_ = used_bytes_;
            }
            ++allocations_;
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
            used_bytes_ = 0;
            ++resets_;
        }

        std::size_t capacity() const noexcept { return storage_.size(); }
        std::size_t used_bytes() const noexcept { return used_bytes_; }
        std::size_t remaining_bytes() const noexcept { return capacity() - used_bytes_; }
        std::size_t peak_bytes() const noexcept { return peak_bytes_; }
        std::uint64_t allocation_count() const noexcept { return allocations_; }
        std::uint64_t reset_count() const noexcept { return resets_; }

      private:
        friend class stack_arena;
        void rewind_to(std::size_t used_bytes) noexcept { used_bytes_ = used_bytes; }

        std::span<std::byte> storage_;
        std::size_t used_bytes_{};
        std::size_t peak_bytes_{};
        std::uint64_t allocations_{};
        std::uint64_t resets_{};
    };

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
                if (owner_ && !owner_->rewind(*this))
                {
                    // Letting an outer scope die while an inner one still owns
                    // its checkpoint would leave a dangling checkpoint chain.
                    std::terminate();
                }
            }

            bool active() const noexcept { return owner_ != nullptr; }
            bool rewind() noexcept { return owner_ && owner_->rewind(*this); }

          private:
            friend class stack_arena;
            explicit scope(stack_arena& owner) noexcept
                : owner_(&owner), previous_(owner.scope_), used_bytes_(owner.used_bytes())
            {
                owner.scope_ = this;
            }

            stack_arena* owner_;
            scope* previous_;
            std::size_t used_bytes_;
        };

        stack_arena() noexcept = default;
        explicit stack_arena(std::span<std::byte> storage) noexcept : arena_(storage) {}
        stack_arena(const stack_arena&) = delete;
        stack_arena& operator=(const stack_arena&) = delete;
        stack_arena(stack_arena&&) = delete;
        stack_arena& operator=(stack_arena&&) = delete;

        ~stack_arena() noexcept
        {
            if (scope_)
            {
                std::terminate();
            }
        }

        [[nodiscard]] scope checkpoint() noexcept { return scope{*this}; }

        bool rewind(scope& checkpoint) noexcept
        {
            if (checkpoint.owner_ != this || scope_ != &checkpoint)
            {
                return false;
            }
            arena_.rewind_to(checkpoint.used_bytes_);
            scope_ = checkpoint.previous_;
            checkpoint.owner_ = nullptr;
            checkpoint.previous_ = nullptr;
            ++rewinds_;
            return true;
        }

        [[nodiscard]] void* try_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) noexcept
        {
            return arena_.try_allocate(bytes, alignment);
        }

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t))
        {
            return arena_.allocate(bytes, alignment);
        }

        bool try_reset() noexcept
        {
            if (scope_)
            {
                return false;
            }
            arena_.reset();
            return true;
        }

        void reset()
        {
            if (!try_reset())
            {
                throw std::logic_error("Stack arena cannot reset while a checkpoint is active.");
            }
        }

        std::size_t capacity() const noexcept { return arena_.capacity(); }
        std::size_t used_bytes() const noexcept { return arena_.used_bytes(); }
        std::size_t remaining_bytes() const noexcept { return arena_.remaining_bytes(); }
        std::size_t peak_bytes() const noexcept { return arena_.peak_bytes(); }
        std::uint64_t allocation_count() const noexcept { return arena_.allocation_count(); }
        std::uint64_t reset_count() const noexcept { return arena_.reset_count(); }
        std::uint64_t rewind_count() const noexcept { return rewinds_; }

      private:
        linear_arena arena_;
        scope* scope_{};
        std::uint64_t rewinds_{};
    };

    // Single-writer, fixed-capacity object storage. The free list occupies unused
    // object slots; a compact live bitmap validates releases and drives teardown.
    // emplace/destroy are O(1); construction/clear are O(capacity_value). The pool itself
    // never allocates. T's constructor/destructor must not reenter this same pool.
    // Returned pointers borrow the pool and become invalid immediately on destroy.
    // As with ordinary raw pointers, same-address reuse does not detect a stale
    // pointer from an earlier occupant; callers needing that require generation handles.
    template<class T, std::size_t capacity_value>
    class fixed_pool final
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>);
        static_assert(capacity_value > 0);

        union slot
        {
            T value;
            std::size_t next;
            slot() noexcept : next(capacity_value) {}
            ~slot() noexcept {}
        };
        static_assert(capacity_value <= (std::numeric_limits<std::size_t>::max)() / sizeof(slot));
        static constexpr std::size_t bits_per_word_ = 64;
        static constexpr std::size_t word_count_ = capacity_value / bits_per_word_ + (capacity_value % bits_per_word_ != 0);

      public:
        fixed_pool() noexcept
        {
            for (std::size_t index = 0; index < capacity_value; ++index)
            {
                slots_[index].next = index + 1;
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
            if (free_ == capacity_value)
            {
                return nullptr;
            }
            const std::size_t index = free_;
            slot& entry = slots_[index];
            free_ = entry.next;
            try
            {
                T* value = std::construct_at(std::addressof(entry.value), std::forward<Args>(args)...);
                live_[index / bits_per_word_] |= std::uint64_t{1} << (index % bits_per_word_);
                ++size_;
                return value;
            }
            catch (...)
            {
                std::construct_at(std::addressof(entry.next), free_);
                free_ = index;
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
            if (index == capacity_value)
            {
                return false;
            }
            destroy_slot(index);
            return true;
        }

        void clear() noexcept
        {
            for (std::size_t index = 0; index < capacity_value; ++index)
            {
                if (is_live(index))
                {
                    destroy_slot(index);
                }
            }
        }

        bool owns(const T* value) const noexcept { return live_index(value) != capacity_value; }
        static constexpr std::size_t capacity() noexcept { return capacity_value; }
        std::size_t size() const noexcept { return size_; }
        std::size_t available() const noexcept { return capacity_value - size_; }
        bool empty() const noexcept { return size_ == 0; }
        bool full() const noexcept { return size_ == capacity_value; }

      private:
        bool is_live(std::size_t index) const noexcept
        {
            return (live_[index / bits_per_word_] & (std::uint64_t{1} << (index % bits_per_word_))) != 0;
        }

        std::size_t live_index(const T* value) const noexcept
        {
            if (!value)
            {
                return capacity_value;
            }
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
            const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(slots_.data());
            if (address < first)
            {
                return capacity_value;
            }
            const std::uintptr_t offset = address - first;
            if (offset % sizeof(slot))
            {
                return capacity_value;
            }
            const std::uintptr_t index = offset / sizeof(slot);
            if (index >= capacity_value || !is_live(static_cast<std::size_t>(index)))
            {
                return capacity_value;
            }
            return static_cast<std::size_t>(index);
        }

        void destroy_slot(std::size_t index) noexcept
        {
            live_[index / bits_per_word_] &= ~(std::uint64_t{1} << (index % bits_per_word_));
            --size_;
            slot& entry = slots_[index];
            std::destroy_at(std::addressof(entry.value));
            std::construct_at(std::addressof(entry.next), free_);
            free_ = index;
        }

        ce::fixed_array<slot, capacity_value> slots_;
        ce::fixed_array<std::uint64_t, word_count_> live_{};
        std::size_t free_{};
        std::size_t size_{};
    };
}
