#ifndef OWN_OWNERSHIP_HPP
#define OWN_OWNERSHIP_HPP

// Copyright (c) 2026 29thnight. Licensed under the MIT License; see LICENSE.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

#ifndef OWN_DEBUG_THREAD_CHECK
#ifdef NDEBUG
#define OWN_DEBUG_THREAD_CHECK 0
#else
#define OWN_DEBUG_THREAD_CHECK 1
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define OWN_NOINLINE __declspec(noinline)
#else
#define OWN_NOINLINE __attribute__((noinline))
#endif

#ifndef OWN_ENABLE_UNSAFE_GET_WARNING
#define OWN_ENABLE_UNSAFE_GET_WARNING 1
#endif

namespace own
{
    template<class T> class unique_owner;
    template<class T> class allocated_unique_owner;
    template<class T> class local_owner;
    template<class T> class shared_owner;
    template<class T> class weak_owner;
    template<class T> class local_view;
    template<class T> class enable_owner_from_this;
    class retirement_task;

    namespace detail
    {
        [[noreturn]] inline void fail_fast() noexcept
        {
            std::abort();
        }

        inline void* default_allocate(void*, std::size_t bytes, std::size_t alignment)
        {
            if (alignment > alignof(std::max_align_t))
            {
                return ::operator new(bytes, std::align_val_t(alignment));
            }
            return ::operator new(bytes);
        }

        inline void default_deallocate(void*, void* pointer, std::size_t,
                                       std::size_t alignment) noexcept
        {
            if (alignment > alignof(std::max_align_t))
            {
                ::operator delete(pointer, std::align_val_t(alignment));
                return;
            }
            ::operator delete(pointer);
        }

        template<class From, class To>
        inline constexpr bool unique_conversion_allowed = []() noexcept
        {
            if constexpr (!std::is_convertible_v<From*, To*>)
            {
                return false;
            }
            else if constexpr (std::is_same_v<std::remove_cv_t<From>, std::remove_cv_t<To>>)
            {
                return true; // Qualification conversion keeps the same delete type.
            }
            else
            {
                // Unlike an erased owner, the default deletes through its view
                // type. A polymorphic upcast must preserve valid destruction.
                return std::has_virtual_destructor_v<To> && std::is_nothrow_destructible_v<To>;
            }
        }();

        using unique_deallocate_function = void (*)(void*, void*, std::size_t, std::size_t) noexcept;
        using unique_dispose_function = void (*)(void*, void*, unique_deallocate_function) noexcept;

        template<class T>
        void dispose_unique(void* storage, void* context, unique_deallocate_function deallocate) noexcept
        {
            // The concrete factory type and original allocation survive every
            // adjusted base/const conversion of the owner handle.
            static_cast<T*>(storage)->~T();
            deallocate(context, storage, sizeof(T), alignof(T));
        }

        struct owner_registration_marker {};
        struct control_block;
        struct owner_access;
        void last_strong_released(control_block*) noexcept;
    }

    // A non-owning byte allocator. Its context must outlive all allocations,
    // including weak handles and deferred retirement tasks. Callbacks must be
    // usable on every thread that can allocate/free these objects.
    struct allocator_ref
    {
        void* context = nullptr;
        void* (*allocate)(void*, std::size_t, std::size_t) = detail::default_allocate;
        void (*deallocate)(void*, void*, std::size_t, std::size_t) noexcept =
            detail::default_deallocate;
    };

    // The callback must be noexcept, and either run the task or move it into
    // a queue. A dropped task runs immediately. The context is non-owning.
    struct retirement_hook
    {
        void* context = nullptr;
        void (*retire)(void*, retirement_task) noexcept = nullptr;
    };

    class retirement_task
    {
    public:
        retirement_task() noexcept = default;
        retirement_task(const retirement_task&) = delete;
        retirement_task& operator=(const retirement_task&) = delete;
        retirement_task(retirement_task&& other) noexcept
            : block_(std::exchange(other.block_, nullptr))
        {
        }
        retirement_task& operator=(retirement_task&& other) noexcept
        {
            if (this != &other)
            {
                run();
                block_ = std::exchange(other.block_, nullptr);
            }
            return *this;
        }
        ~retirement_task() { run(); }
        explicit operator bool() const noexcept { return block_ != nullptr; }
        void run() noexcept;

    private:
        explicit retirement_task(detail::control_block* block) noexcept : block_(block) {}
        detail::control_block* block_ = nullptr;
        friend void detail::last_strong_released(detail::control_block*) noexcept;
    };

    namespace detail
    {
        // Non-atomic counts (local alias counts, thread IDs) use the full range.
        inline constexpr std::size_t count_limit = static_cast<std::size_t>(-1);

        using deallocate_function = void (*)(void*, void*, std::size_t, std::size_t) noexcept;

        // Per-block-type operations, one static table per instantiation, so a
        // block header stores a single pointer instead of every callback.
        struct control_ops
        {
            void (*dispose)(control_block*) noexcept;
            void (*destroy)(control_block*) noexcept;
            // Null for compact blocks, which have no retirement hook.
            retirement_hook* (*retirement)(control_block*) noexcept;
        };

        // Both reference counts share one 64-bit word: strong in the low half,
        // weak in the high half. One load then shows the last owner whether any
        // other reference exists, so a never-shared object is destroyed without
        // a locked read-modify-write.
        using count_word = std::uint64_t;
        inline constexpr count_word strong_one = 1;
        inline constexpr count_word weak_one = count_word{1} << 32;
        inline constexpr count_word strong_mask = weak_one - 1;
        // Each half aborts at 2^31. The other 2^31 values of that half absorb
        // increments already in flight, so a count can neither wrap through zero
        // nor carry into the other half.
        inline constexpr count_word saturation_limit = count_word{1} << 31;
        inline constexpr count_word unique_counts = strong_one | weak_one;

        inline constexpr std::size_t strong_of(count_word counts) noexcept
        {
            return static_cast<std::size_t>(counts & strong_mask);
        }
        inline constexpr std::size_t weak_of(count_word counts) noexcept
        {
            return static_cast<std::size_t>(counts >> 32);
        }

        // The common header: 2 words. Allocator and retirement state live only
        // in the extended block used by allocate_* and *_with factories.
        struct control_block
        {
            // One implicit weak reference covers the entire live or retired
            // object lifetime, plus one per external weak_owner.
            std::atomic<count_word> counts{unique_counts};
            const control_ops* ops;

            explicit control_block(const control_ops* operations) noexcept : ops(operations) {}
            std::size_t use_count() const noexcept
            {
                return strong_of(counts.load(std::memory_order_relaxed));
            }
        };

        // One locked add rather than a compare-exchange loop, which costs extra
        // transfers of a contended line. Every caller already holds a reference
        // of the kind it adds (or a strong one, for weak), so a previous count of
        // zero is a use-after-release bug.
        inline void add_strong(control_block* block) noexcept
        {
            const auto previous = strong_of(block->counts.fetch_add(strong_one, std::memory_order_relaxed));
            if (previous == 0 || previous >= saturation_limit) [[unlikely]] { fail_fast(); }
        }
        inline void add_weak(control_block* block) noexcept
        {
            const auto previous = weak_of(block->counts.fetch_add(weak_one, std::memory_order_relaxed));
            if (previous == 0 || previous >= saturation_limit) [[unlikely]] { fail_fast(); }
        }

        inline void release_weak(control_block* block) noexcept
        {
            // No strong reference and only the caller's weak one: nothing else
            // can reach the block, so it needs no read-modify-write. This load
            // is cheap where it matters: right after the last strong release,
            // which already holds the line.
            if (block->counts.load(std::memory_order_acquire) == weak_one ||
                weak_of(block->counts.fetch_sub(weak_one, std::memory_order_acq_rel)) == 1)
            {
                block->ops->destroy(block);
            }
        }

        inline void last_strong_released(control_block* block) noexcept
        {
            // Zero is permanent. Keeping the implicit weak alive permits
            // deferred destruction without successful weak locking.
            retirement_hook hook;
            if (auto* hook_of = block->ops->retirement) { hook = *hook_of(block); }
            retirement_task task(block);
            if (hook.retire) { hook.retire(hook.context, std::move(task)); }
        }

        inline void release_strong(control_block* block) noexcept
        {
            // No load before the decrement: on a line other cores are also
            // updating, a separate load costs another transfer of the line.
            // Release publishes this owner's accesses; only the thread dropping
            // the last reference needs acquire. A load rather than a fence keeps
            // ThreadSanitizer able to model the ordering.
            if (strong_of(block->counts.fetch_sub(strong_one, std::memory_order_release)) == 1)
            {
                (void)block->counts.load(std::memory_order_acquire);
                // If no weak observer existed, none can appear now that strong
                // is zero: release_weak then sees weak_one and frees the block
                // without a second read-modify-write.
                last_strong_released(block);
            }
        }

        // For a handle that is likely the only reference (see shared_owner's
        // fresh hint). One strong reference and only the implicit weak means no
        // other owner, observer or registration exists, and none can appear
        // without one, so no other thread can touch the counts. The acquire
        // load still orders earlier releases by other threads before disposal.
        OWN_NOINLINE inline void release_strong_if_sole(control_block* block) noexcept
        {
            if (block->counts.load(std::memory_order_acquire) == unique_counts)
            {
                block->counts.store(weak_one, std::memory_order_relaxed);
                last_strong_released(block);
                return;
            }
            release_strong(block);
        }

        inline bool try_add_strong(control_block* block) noexcept
        {
            auto value = block->counts.load(std::memory_order_relaxed);
            while (strong_of(value) != 0)
            {
                if (strong_of(value) >= saturation_limit) { fail_fast(); }
                if (block->counts.compare_exchange_weak(value, value + strong_one,
                        std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return true;
                }
            }
            return false;
        }

        inline void* allocate_bytes(allocator_ref allocator, std::size_t size,
                                    std::size_t alignment)
        {
            // The default allocator is a known function: call it directly so it
            // inlines into ::operator new, which never returns null.
            if (allocator.allocate == default_allocate && allocator.deallocate)
            {
                return default_allocate(allocator.context, size, alignment);
            }
            if (!allocator.allocate || !allocator.deallocate) { fail_fast(); }
            void* result = allocator.allocate(allocator.context, size, alignment);
            if (!result) { throw std::bad_alloc(); }
            return result;
        }

        inline std::size_t current_thread_id() noexcept
        {
            // Unique monotonic IDs also catch destruction on a newly created
            // thread after the origin thread has exited (TLS addresses reuse).
            // IDs start at 1: zero marks a group created without checks.
            static std::atomic<std::size_t> next{1};
            thread_local const std::size_t id = []() noexcept
            {
                auto value = next.load(std::memory_order_relaxed);
                for (;;)
                {
                    if (value == count_limit) { fail_fast(); }
                    if (next.compare_exchange_weak(value, value + 1,
                            std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        return value;
                    }
                }
            }();
            return id;
        }

        struct local_group
        {
            // Alias count in steps of `one_alias`; the low bit marks an exclusive
            // group: its strong reference is the only reference of any kind to
            // the block, which only a local factory can establish and only this
            // thread can end (share(), weak observation). Packed here so the
            // group stays five words, and in the low bit so counting and the
            // zero test stay single compares.
            static constexpr std::size_t exclusive_bit = 1;
            static constexpr std::size_t one_alias = 2;
            std::size_t references = one_alias;
            std::size_t alias_count() const noexcept { return references / one_alias; }
            void end_exclusive() noexcept
            {
                // Writes only when set: a store right before the caller's locked
                // increment would make that instruction wait for it.
                if (references & exclusive_bit) { references &= ~exclusive_bit; }
            }
            control_block* block;
            // Only what release() needs: 2 words rather than a whole allocator_ref.
            void* context;
            deallocate_function deallocate;
            // Present in every configuration: translation units that disagree on
            // NDEBUG/OWN_DEBUG_THREAD_CHECK must still agree on size and offsets,
            // because groups are allocated, read and freed across them. Zero means
            // the group was created without checks; such a group is never checked.
            const std::size_t thread_id = OWN_DEBUG_THREAD_CHECK ? current_thread_id() : 0;
            local_group(control_block* control, allocator_ref resource) noexcept
                : block(control), context(resource.context), deallocate(resource.deallocate)
            {
            }
            void check_thread() const noexcept
            {
#if OWN_DEBUG_THREAD_CHECK
                if (thread_id != 0 && thread_id != current_thread_id()) { fail_fast(); }
#endif
            }
            void add_reference() noexcept
            {
                check_thread();
                if (references >= count_limit - exclusive_bit) { fail_fast(); }
                references += one_alias;
            }
            void release() noexcept
            {
                check_thread();
                references -= one_alias;
                if (references < one_alias) [[unlikely]] { end(); }
            }
            // Out of line so that copies and drops of aliases, which happen in
            // loops, inline as a subtract and a compare.
            OWN_NOINLINE void end() noexcept
            {
                // An exclusive group's strong reference is the only reference of
                // any kind, and none can be created except through this group, so
                // no other thread can touch the counts: no atomic decrement.
                const bool exclusive = references == exclusive_bit;
                auto* control = block;
                auto* resource = context;
                auto release_storage = deallocate;
                this->~local_group();
                release_storage(resource, this, sizeof(local_group), alignof(local_group));
                if (exclusive)
                {
                    control->counts.store(weak_one, std::memory_order_relaxed);
                    last_strong_released(control);
                }
                else
                {
                    release_strong(control);
                }
            }
        };

        // Default-allocator groups keep one freed group's storage per thread for
        // the next localize(). A group is created and released on the same
        // thread, so the slot needs no synchronization; it is freed at thread
        // exit. The state is trivially destructible so a group released during
        // later thread-exit destruction can still see that caching has ended.
        struct group_cache_state
        {
            void* slot = nullptr;
            bool ended = false;
        };
        inline group_cache_state& group_cache() noexcept
        {
            thread_local constinit group_cache_state state;
            return state;
        }
        struct group_cache_cleanup
        {
            ~group_cache_cleanup()
            {
                auto& cache = group_cache();
                cache.ended = true;
                if (cache.slot)
                {
                    default_deallocate(nullptr, std::exchange(cache.slot, nullptr), sizeof(local_group),
                                       alignof(local_group));
                }
            }
        };
        inline void cached_group_release(void*, void* storage, std::size_t, std::size_t) noexcept
        {
            auto& cache = group_cache();
            if (!cache.slot && !cache.ended)
            {
                // Registers the thread-exit cleanup on first use in this thread.
                thread_local group_cache_cleanup cleanup;
                (void)cleanup;
                cache.slot = storage;
                return;
            }
            default_deallocate(nullptr, storage, sizeof(local_group), alignof(local_group));
        }

        inline local_group* new_group(control_block* block, allocator_ref allocator)
        {
            if (allocator.allocate == default_allocate && allocator.deallocate == default_deallocate &&
                allocator.context == nullptr)
            {
                auto& cache = group_cache();
                void* storage = cache.slot ? std::exchange(cache.slot, nullptr)
                                           : default_allocate(nullptr, sizeof(local_group), alignof(local_group));
                allocator_ref cached{nullptr, default_allocate, cached_group_release};
                return ::new (storage) local_group(block, cached);
            }
            void* storage = allocate_bytes(allocator, sizeof(local_group), alignof(local_group));
            return ::new (storage) local_group(block, allocator);
        }
        template<class Block>
        inline constexpr control_ops ops_for{Block::dispose_object, Block::destroy_control,
                                             Block::retirement_of};

        // make_local and allocate_local reserve storage for their first local
        // group inside the block, so creating a local owner is one allocation.
        // The group's lifetime is always within the block's: it holds a strong
        // reference until its last alias is released, and its "deallocation"
        // then only ends the group object's lifetime.
        struct no_group_slot {};
        struct group_slot
        {
            alignas(local_group) unsigned char storage[sizeof(local_group)];
        };
        inline void embedded_group_release(void*, void*, std::size_t, std::size_t) noexcept {}

        // make_shared/make_local: default allocator, no hook. Header plus payload
        // (plus the first group for make_local), laid out like the standard
        // library's in-place block.
        template<class T, bool Group = false>
        struct in_place_control final : control_block
        {
            [[no_unique_address]] std::conditional_t<Group, group_slot, no_group_slot> group;
            alignas(T) unsigned char storage[sizeof(T)];

            template<class... Args>
            explicit in_place_control(Args&&... args)
                : control_block(&ops_for<in_place_control>)
            {
                ::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
            }

            T* pointer() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }

            static void dispose_object(control_block* base) noexcept
            {
                static_cast<in_place_control*>(base)->pointer()->~T();
            }
            static void destroy_control(control_block* base) noexcept
            {
                auto* block = static_cast<in_place_control*>(base);
                block->~in_place_control();
                default_deallocate(nullptr, block, sizeof(in_place_control), alignof(in_place_control));
            }
            static constexpr retirement_hook* (*retirement_of)(control_block*) noexcept = nullptr;
        };

        // allocate_* and *_with factories: carries the deallocation callback and
        // retirement hook. The allocate callback is not needed after allocation.
        template<class T, bool Group = false>
        struct allocated_control final : control_block
        {
            void* context;
            deallocate_function deallocate;
            retirement_hook retirement;
            [[no_unique_address]] std::conditional_t<Group, group_slot, no_group_slot> group;
            alignas(T) unsigned char storage[sizeof(T)];

            template<class... Args>
            allocated_control(allocator_ref allocator, retirement_hook hook, Args&&... args)
                : control_block(&ops_for<allocated_control>), context(allocator.context),
                  deallocate(allocator.deallocate), retirement(hook)
            {
                ::new (static_cast<void*>(storage)) T(std::forward<Args>(args)...);
            }

            T* pointer() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }

            static void dispose_object(control_block* base) noexcept
            {
                static_cast<allocated_control*>(base)->pointer()->~T();
            }
            static void destroy_control(control_block* base) noexcept
            {
                auto* block = static_cast<allocated_control*>(base);
                auto* resource = block->context;
                auto release = block->deallocate;
                block->~allocated_control();
                release(resource, block, sizeof(allocated_control), alignof(allocated_control));
            }
            static retirement_hook* retirement_of(control_block* base) noexcept
            {
                return &static_cast<allocated_control*>(base)->retirement;
            }
        };

    }

    inline void retirement_task::run() noexcept
    {
        if (auto* block = std::exchange(block_, nullptr))
        {
            block->ops->dispose(block);
            detail::release_weak(block);
        }
    }

    // Non-owning access only. Copying a view does not retain or inspect any
    // ownership record. The caller keeps an owner alive for every use.
    template<class T>
    class local_view
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        constexpr local_view() noexcept = default;
        constexpr local_view(std::nullptr_t) noexcept {}
        template<class U> requires std::is_convertible_v<U*, T*>
        constexpr local_view(const local_view<U>& other) noexcept : pointer_(other.pointer_) {}
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        constexpr T* unsafe_get() const noexcept { return pointer_; }
        constexpr explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend constexpr bool operator==(const local_view& handle, std::nullptr_t) noexcept { return !handle; }
        constexpr T& operator*() const noexcept { return *pointer_; }
        constexpr T* operator->() const noexcept { return pointer_; }
        constexpr void reset() noexcept { pointer_ = nullptr; }
        constexpr void swap(local_view& other) noexcept { std::swap(pointer_, other.pointer_); }

    private:
        T* pointer_ = nullptr;
        explicit constexpr local_view(T* pointer) noexcept : pointer_(pointer) {}
        template<class> friend class local_view;
        template<class> friend class unique_owner;
        template<class> friend class allocated_unique_owner;
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class enable_owner_from_this;
    };

    // Default exclusive ownership is just one pointer. Typed new/delete supply
    // destruction and allocation lookup without runtime allocator metadata.
    template<class T>
    class unique_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        unique_owner() noexcept = default;
        unique_owner(std::nullptr_t) noexcept {}
        unique_owner(const unique_owner&) = delete;
        unique_owner& operator=(const unique_owner&) = delete;
        unique_owner(unique_owner&& other) noexcept
            : pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires detail::unique_conversion_allowed<U, T>
        unique_owner(unique_owner<U>&& other) noexcept
            : pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        ~unique_owner() { destroy(pointer_); }
        unique_owner& operator=(unique_owner&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires detail::unique_conversion_allowed<U, T>
        unique_owner& operator=(unique_owner<U>&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { destroy(std::exchange(pointer_, nullptr)); }
        void swap(unique_owner& other) noexcept { std::swap(pointer_, other.pointer_); }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend bool operator==(const unique_owner& handle, std::nullptr_t) noexcept { return !handle; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }

    private:
        T* pointer_ = nullptr;
        static void destroy(T* pointer) noexcept
        {
            static_assert(sizeof(T) > 0, "unique_owner destruction requires a complete payload type");
            static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
            if (pointer) { delete pointer; }
        }
        explicit unique_owner(T* pointer) noexcept : pointer_(pointer) {}
        template<class> friend class unique_owner;
        friend struct detail::owner_access;
    };

    // Explicit allocator-aware ownership. Erased allocator/destruction state
    // lives in this opt-in handle, never in the pointer-only default owner.
    template<class T>
    class allocated_unique_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        allocated_unique_owner() noexcept = default;
        allocated_unique_owner(std::nullptr_t) noexcept {}
        allocated_unique_owner(const allocated_unique_owner&) = delete;
        allocated_unique_owner& operator=(const allocated_unique_owner&) = delete;
        allocated_unique_owner(allocated_unique_owner&& other) noexcept { take_from(other); }
        template<class U> requires std::is_convertible_v<U*, T*>
        allocated_unique_owner(allocated_unique_owner<U>&& other) noexcept { take_from(other); }
        ~allocated_unique_owner() { reset(); }
        allocated_unique_owner& operator=(allocated_unique_owner&& other) noexcept
        {
            move_assign(other);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        allocated_unique_owner& operator=(allocated_unique_owner<U>&& other) noexcept
        {
            move_assign(other);
            return *this;
        }
        void reset() noexcept
        {
            if (!pointer_) { return; }
            auto* storage = storage_;
            auto* context = context_;
            auto deallocate = deallocate_;
            auto dispose = dispose_;
            pointer_ = nullptr;
            // Empty before callbacks; do not touch this again, because user
            // cleanup may install a replacement owner through a saved handle.
            dispose(storage, context, deallocate);
        }
        void swap(allocated_unique_owner& other) noexcept
        {
            if (pointer_ && other.pointer_)
            {
                std::swap(pointer_, other.pointer_);
                std::swap(storage_, other.storage_);
                std::swap(context_, other.context_);
                std::swap(deallocate_, other.deallocate_);
                std::swap(dispose_, other.dispose_);
            }
            else if (pointer_)
            {
                other.take_from(*this);
            }
            else if (other.pointer_)
            {
                take_from(other);
            }
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend bool operator==(const allocated_unique_owner& handle, std::nullptr_t) noexcept { return !handle; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }

    private:
        // pointer_ is the sole ownership indicator. The four cleanup fields
        // are meaningful/readable only when pointer_ is non-null.
        T* pointer_ = nullptr;
        void* storage_ = nullptr;
        void* context_ = nullptr;
        detail::unique_deallocate_function deallocate_ = nullptr;
        detail::unique_dispose_function dispose_ = nullptr;

        allocated_unique_owner(T* pointer, void* storage, allocator_ref allocator,
                     detail::unique_dispose_function dispose) noexcept
            : pointer_(pointer), storage_(storage), context_(allocator.context),
              deallocate_(allocator.deallocate), dispose_(dispose)
        {
        }
        // Same observable order as constructing a temporary and swapping: this
        // handle holds the new object before the old one is disposed, so cleanup
        // may reassign it. Avoids the temporary and swap's emptiness branches.
        template<class U>
        void move_assign(allocated_unique_owner<U>& other) noexcept
        {
            if constexpr (std::is_same_v<U, T>)
            {
                if (&other == this) { return; }
            }
            T* const previous = pointer_;
            if (!previous)
            {
                take_from(other);
                return;
            }
            void* const storage = storage_;
            void* const context = context_;
            const auto deallocate = deallocate_;
            const auto dispose = dispose_;
            pointer_ = nullptr;
            take_from(other);
            dispose(storage, context, deallocate);
        }
        template<class U>
        void take_from(allocated_unique_owner<U>& other) noexcept
        {
            // Called only for a new or logically empty destination. Inactive
            // cleanup fields may hold invalid pointer values after the old
            // allocation/context dies: never copy/read them on an empty path.
            pointer_ = other.pointer_;
            if (!pointer_) { return; }
            storage_ = other.storage_;
            context_ = other.context_;
            deallocate_ = other.deallocate_;
            dispose_ = other.dispose_;
            other.pointer_ = nullptr;
        }
        template<class> friend class allocated_unique_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    class shared_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        shared_owner() noexcept = default;
        shared_owner(std::nullptr_t) noexcept {}
        // A copy never inherits the fresh hint: only the handle a factory
        // returned (and whatever it is moved into) can be the sole reference.
        shared_owner(const shared_owner& other) noexcept
            : bits_(other.bits_ & ~fresh_hint), pointer_(other.pointer_)
        {
            if (bits_) { detail::add_strong(block()); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner(const shared_owner<U>& other) noexcept
            : bits_(other.bits_ & ~fresh_hint), pointer_(other.pointer_)
        {
            if (bits_) { detail::add_strong(block()); }
        }
        shared_owner(shared_owner&& other) noexcept
            : bits_(std::exchange(other.bits_, 0)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner(shared_owner<U>&& other) noexcept
            : bits_(std::exchange(other.bits_, 0)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        ~shared_owner()
        {
            // Unhinted bits are the block address itself, so the common path
            // decrements without masking. The hinted path is out of line, so
            // copy/drop loops keep only a bit test beside the decrement.
            const auto bits = bits_;
            if (bits & fresh_hint) [[unlikely]]
            {
                detail::release_strong_if_sole(reinterpret_cast<detail::control_block*>(bits - fresh_hint));
            }
            else if (bits) [[likely]]
            {
                detail::release_strong(reinterpret_cast<detail::control_block*>(bits));
            }
        }
        shared_owner& operator=(const shared_owner& other) noexcept
        {
            shared_owner(other).swap(*this);
            return *this;
        }
        shared_owner& operator=(shared_owner&& other) noexcept
        {
            shared_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner& operator=(const shared_owner<U>& other) noexcept
        {
            shared_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        shared_owner& operator=(shared_owner<U>&& other) noexcept
        {
            shared_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { shared_owner().swap(*this); }
        void swap(shared_owner& other) noexcept
        {
            std::swap(bits_, other.bits_);
            std::swap(pointer_, other.pointer_);
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept { return local_view<T>(pointer_); }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend bool operator==(const shared_owner& handle, std::nullptr_t) noexcept { return !handle; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }
        std::size_t use_count() const noexcept
        {
            return bits_ ? block()->use_count() : 0;
        }
        [[nodiscard]] local_owner<T> localize(allocator_ref allocator = {}) const &;
        [[nodiscard]] local_owner<T> localize(allocator_ref allocator = {}) &&;

    private:
        // The control block address, with its low bit as a hint: set on the
        // handle a factory returned and kept across moves, cleared on every
        // copy. Only a hinted handle checks, on release, whether it holds the
        // sole reference and can skip the atomic decrement; copies release
        // with one fetch_sub as before. The hint selects a path only: the
        // check itself decides, so a stale hint costs a load, never safety.
        static constexpr std::uintptr_t fresh_hint = 1;
        static_assert(alignof(detail::control_block) > fresh_hint);
        std::uintptr_t bits_ = 0;
        T* pointer_ = nullptr;
        detail::control_block* block() const noexcept
        {
            return reinterpret_cast<detail::control_block*>(bits_ & ~fresh_hint);
        }
        shared_owner(detail::control_block* block, T* pointer, bool fresh = false) noexcept
            : bits_(reinterpret_cast<std::uintptr_t>(block) | (fresh ? fresh_hint : 0)), pointer_(pointer)
        {
        }
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class weak_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    class local_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        local_owner() noexcept = default;
        local_owner(std::nullptr_t) noexcept {}
        local_owner(const local_owner& other) noexcept
            : group_(other.group_), pointer_(other.pointer_)
        {
            if (group_) { group_->add_reference(); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner(const local_owner<U>& other) noexcept
            : group_(other.group_), pointer_(other.pointer_)
        {
            if (group_) { group_->add_reference(); }
        }
        local_owner(local_owner&& other) noexcept
        {
            other.check_thread();
            group_ = std::exchange(other.group_, nullptr);
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner(local_owner<U>&& other) noexcept
        {
            other.check_thread();
            group_ = std::exchange(other.group_, nullptr);
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        ~local_owner() { if (group_) { group_->release(); } }
        local_owner& operator=(const local_owner& other) noexcept
        {
            local_owner(other).swap(*this);
            return *this;
        }
        local_owner& operator=(local_owner&& other) noexcept
        {
            local_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner& operator=(const local_owner<U>& other) noexcept
        {
            local_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        local_owner& operator=(local_owner<U>&& other) noexcept
        {
            local_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { local_owner().swap(*this); }
        void swap(local_owner& other) noexcept
        {
            check_thread();
            other.check_thread();
            std::swap(group_, other.group_);
            std::swap(pointer_, other.pointer_);
        }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { check_thread(); return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] local_view<T> borrow() const & noexcept
        {
            check_thread();
            return local_view<T>(pointer_);
        }
        local_view<T> borrow() const && = delete;
        explicit operator bool() const noexcept { check_thread(); return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered. Uses the same thread check as operator bool.
        friend bool operator==(const local_owner& handle, std::nullptr_t) noexcept { return !handle; }
        T& operator*() const noexcept { check_thread(); return *pointer_; }
        T* operator->() const noexcept { check_thread(); return pointer_; }
        std::size_t use_count() const noexcept
        {
            check_thread();
            return group_ ? group_->block->use_count() : 0;
        }
        std::size_t local_use_count() const noexcept
        {
            check_thread();
            return group_ ? group_->alias_count() : 0;
        }
        [[nodiscard]] shared_owner<T> share() const noexcept
        {
            check_thread();
            if (!group_) { return {}; }
            group_->end_exclusive();
            detail::add_strong(group_->block);
            return shared_owner<T>(group_->block, pointer_);
        }

    private:
        detail::local_group* group_ = nullptr;
        T* pointer_ = nullptr;
        void check_thread() const noexcept { if (group_) { group_->check_thread(); } }
        local_owner(detail::local_group* group, T* pointer) noexcept
            : group_(group), pointer_(pointer)
        {
        }
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class weak_owner;
        friend struct detail::owner_access;
    };

    template<class T>
    local_owner<T> shared_owner<T>::localize(allocator_ref allocator) const &
    {
        if (!bits_) { return {}; }
        // Allocate first. Failure leaves this owner and its count unchanged.
        auto* group = detail::new_group(block(), allocator);
        detail::add_strong(block());
        return local_owner<T>(group, pointer_);
    }

    template<class T>
    local_owner<T> shared_owner<T>::localize(allocator_ref allocator) &&
    {
        if (!bits_) { return {}; }
        auto* group = detail::new_group(block(), allocator);
        auto* pointer = std::exchange(pointer_, nullptr);
        bits_ = 0; // Transfer this global strong reference into the group.
        return local_owner<T>(group, pointer);
    }

    template<class T>
    class weak_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        weak_owner() noexcept = default;
        weak_owner(const weak_owner& other) noexcept
            : block_(other.block_), pointer_(other.pointer_)
        {
            if (block_) { detail::add_weak(block_); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const weak_owner<U>& other) noexcept
            : block_(other.block_)
        {
            if (block_)
            {
                detail::add_weak(block_);
                // Derived-to-virtual-base adjustment can read the object.
                // Never adjust an expired pointer, even though its storage
                // remains allocated for the weak control block.
                if (detail::try_add_strong(block_))
                {
                    pointer_ = other.pointer_;
                    detail::release_strong(block_);
                }
            }
        }
        weak_owner(weak_owner&& other) noexcept
            : block_(std::exchange(other.block_, nullptr)),
              pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(weak_owner<U>&& other) noexcept
            : weak_owner(static_cast<const weak_owner<U>&>(other))
        {
            other.reset();
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const shared_owner<U>& other) noexcept
            : block_(other.block()), pointer_(other.pointer_)
        {
            if (block_) { detail::add_weak(block_); }
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner(const local_owner<U>& other) noexcept
        {
            other.check_thread();
            if (other.group_)
            {
                other.group_->end_exclusive();
                block_ = other.group_->block;
                pointer_ = other.pointer_;
                detail::add_weak(block_);
            }
        }
        ~weak_owner() { if (block_) { detail::release_weak(block_); } }
        weak_owner& operator=(const weak_owner& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        weak_owner& operator=(weak_owner&& other) noexcept
        {
            weak_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const weak_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(weak_owner<U>&& other) noexcept
        {
            weak_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const shared_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        weak_owner& operator=(const local_owner<U>& other) noexcept
        {
            weak_owner(other).swap(*this);
            return *this;
        }
        void reset() noexcept { weak_owner().swap(*this); }
        void swap(weak_owner& other) noexcept
        {
            std::swap(block_, other.block_);
            std::swap(pointer_, other.pointer_);
        }
        std::size_t use_count() const noexcept
        {
            return block_ ? block_->use_count() : 0;
        }
        bool expired() const noexcept { return use_count() == 0; }
        [[nodiscard]] shared_owner<T> lock() const noexcept
        {
            if (block_ && detail::try_add_strong(block_))
            {
                return shared_owner<T>(block_, pointer_);
            }
            return {};
        }

    private:
        detail::control_block* block_ = nullptr;
        T* pointer_ = nullptr;
        template<class> friend class weak_owner;
        template<class> friend class enable_owner_from_this;
    };

    // Registration is factory-only. This weak reference never owns the object.
    // Copy/move construction starts unregistered; assignment keeps the target's
    // registration, just as assigning payload must not replace its owner block.
    template<class T>
    class enable_owner_from_this : public detail::owner_registration_marker
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        [[nodiscard]] shared_owner<T> shared_from_this() noexcept { return weak_.lock(); }
        [[nodiscard]] shared_owner<const T> shared_from_this() const noexcept { return weak_.lock(); }
        [[nodiscard]] weak_owner<T> weak_from_this() noexcept { return weak_; }
        [[nodiscard]] weak_owner<const T> weak_from_this() const noexcept { return weak_; }
        [[nodiscard]] local_view<T> borrow_from_this() & noexcept
        {
            return weak_.expired() ? local_view<T>() : local_view<T>(weak_.pointer_);
        }
        [[nodiscard]] local_view<const T> borrow_from_this() const & noexcept
        {
            return weak_.expired() ? local_view<const T>() : local_view<const T>(weak_.pointer_);
        }
        local_view<T> borrow_from_this() && = delete;
        local_view<const T> borrow_from_this() const && = delete;
        [[nodiscard]] local_view<T> local_from_this() & noexcept { return borrow_from_this(); }
        [[nodiscard]] local_view<const T> local_from_this() const & noexcept { return borrow_from_this(); }
        local_view<T> local_from_this() && = delete;
        local_view<const T> local_from_this() const && = delete;

    protected:
        constexpr enable_owner_from_this() noexcept = default;
        enable_owner_from_this(const enable_owner_from_this&) noexcept {}
        enable_owner_from_this(enable_owner_from_this&&) noexcept {}
        enable_owner_from_this& operator=(const enable_owner_from_this&) noexcept { return *this; }
        enable_owner_from_this& operator=(enable_owner_from_this&&) noexcept { return *this; }
        ~enable_owner_from_this() = default;

    private:
        mutable weak_owner<T> weak_;
        friend const enable_owner_from_this* owner_from_this_base(
            detail::control_block*, const enable_owner_from_this* base) noexcept
        {
            return base;
        }
        template<class U>
        void accept_owner(detail::control_block* block, U* object) const noexcept
        {
            static_assert(std::is_convertible_v<std::remove_cv_t<U>*, T*>,
                          "enable_owner_from_this must name a public unambiguous object base");
            if (weak_.block_) { detail::fail_fast(); }
            // Const factories bind the same registration; their const member
            // accessors expose only const ownership/views, as expected.
            weak_.pointer_ = const_cast<std::remove_cv_t<U>*>(object);
            weak_.block_ = block;
            detail::add_weak(block);
        }
        friend struct detail::owner_access;
    };

    namespace detail
    {
        struct owner_access
        {
            template<class T>
            static unique_owner<T> adopt_unique(T* pointer) noexcept
            {
                return unique_owner<T>(pointer);
            }
            template<class T>
            static allocated_unique_owner<T> adopt_allocated_unique(T* pointer, void* storage,
                                                                   allocator_ref allocator) noexcept
            {
                return allocated_unique_owner<T>(pointer, storage, allocator, dispose_unique<T>);
            }
            template<class T>
            static shared_owner<T> adopt(control_block* block, T* pointer) noexcept
            {
                return shared_owner<T>(block, pointer, true); // a factory's sole handle
            }
            template<class T>
            static void bind_from_this(control_block* block, T* object) noexcept
            {
                if constexpr (std::is_base_of_v<owner_registration_marker, std::remove_cv_t<T>>)
                {
                    static_assert(requires { owner_from_this_base(block, object); },
                                  "enable_owner_from_this must be a single public unambiguous base");
                    if constexpr (requires { owner_from_this_base(block, object); })
                    {
                        owner_from_this_base(block, object)->accept_owner(block, object);
                    }
                }
            }
            // Gives a new block's strong reference to the group slot inside it.
            // Nothing can fail: the slot was allocated with the block.
            template<class T, class Block>
            static local_owner<T> into_embedded_group(Block* block) noexcept
            {
                allocator_ref no_storage{nullptr, nullptr, embedded_group_release};
                auto* group = ::new (static_cast<void*>(block->group.storage)) local_group(block, no_storage);
                // Fresh from the factory and unpublished: exclusive unless the
                // payload registered ownership from this (an extra weak).
                if (block->counts.load(std::memory_order_relaxed) == unique_counts)
                {
                    group->references |= local_group::exclusive_bit;
                }
                return local_owner<T>(group, block->pointer());
            }
        };
    }

    template<class T, class... Args>
    [[nodiscard]] allocated_unique_owner<T> allocate_unique(allocator_ref allocator, Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
        void* storage = detail::allocate_bytes(allocator, sizeof(T), alignof(T));
        T* pointer;
        try
        {
            pointer = ::new (storage) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            allocator.deallocate(allocator.context, storage, sizeof(T), alignof(T));
            throw;
        }
        // Unique ownership does not create/register any shared or weak control.
        return detail::owner_access::adopt_allocated_unique(pointer, storage, allocator);
    }

    template<class T, class... Args>
    [[nodiscard]] unique_owner<T> make_unique(Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
        // Ordinary typed new/delete honor the same class-specific allocation
        // functions, including language-provided constructor-failure cleanup.
        return detail::owner_access::adopt_unique(new T(std::forward<Args>(args)...));
    }

    namespace detail
    {
        // Allocates and constructs one block; a throwing payload constructor
        // returns the storage to the allocator that provided it.
        template<class T, class Block, class... Args>
        Block* create_block(allocator_ref allocator, Args&&... args)
        {
            static_assert(std::is_object_v<T> && !std::is_array_v<T>);
            static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
            void* storage = allocate_bytes(allocator, sizeof(Block), alignof(Block));
            Block* block;
            try
            {
                block = ::new (storage) Block(std::forward<Args>(args)...);
            }
            catch (...)
            {
                allocator.deallocate(allocator.context, storage, sizeof(Block), alignof(Block));
                throw;
            }
            owner_access::bind_from_this(block, block->pointer());
            return block;
        }

        template<class T, class Block, class... Args>
        shared_owner<T> create_shared(allocator_ref allocator, Args&&... args)
        {
            Block* block = create_block<T, Block>(allocator, std::forward<Args>(args)...);
            return owner_access::adopt(block, block->pointer());
        }
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> allocate_shared_with(allocator_ref allocator,
                                                       retirement_hook hook, Args&&... args)
    {
        return detail::create_shared<T, detail::allocated_control<T>>(
            allocator, allocator, hook, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> allocate_shared(allocator_ref allocator, Args&&... args)
    {
        return own::allocate_shared_with<T>(allocator, {}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> make_shared_with(retirement_hook hook, Args&&... args)
    {
        return own::allocate_shared_with<T>({}, hook, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] shared_owner<T> make_shared(Args&&... args)
    {
        return detail::create_shared<T, detail::in_place_control<T>>({}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> allocate_local_with(allocator_ref allocator,
                                                     retirement_hook hook, Args&&... args)
    {
        // One allocation holds the block, payload and first group, so the hook
        // can be installed at construction: a factory that fails has no object.
        using block_type = detail::allocated_control<T, true>;
        return detail::owner_access::into_embedded_group<T>(
            detail::create_block<T, block_type>(allocator, allocator, hook, std::forward<Args>(args)...));
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> allocate_local(allocator_ref allocator, Args&&... args)
    {
        return own::allocate_local_with<T>(allocator, {}, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> make_local_with(retirement_hook hook, Args&&... args)
    {
        return own::allocate_local_with<T>({}, hook, std::forward<Args>(args)...);
    }

    template<class T, class... Args>
    [[nodiscard]] local_owner<T> make_local(Args&&... args)
    {
        // Compact block with the first group inside it: one allocation.
        using block_type = detail::in_place_control<T, true>;
        return detail::owner_access::into_embedded_group<T>(
            detail::create_block<T, block_type>({}, std::forward<Args>(args)...));
    }

    template<class T> void swap(unique_owner<T>& a, unique_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(allocated_unique_owner<T>& a, allocated_unique_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_view<T>& a, local_view<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(shared_owner<T>& a, shared_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_owner<T>& a, local_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(weak_owner<T>& a, weak_owner<T>& b) noexcept { a.swap(b); }
}

#endif
