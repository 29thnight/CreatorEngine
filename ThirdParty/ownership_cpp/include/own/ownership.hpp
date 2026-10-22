#ifndef OWN_OWNERSHIP_HPP
#define OWN_OWNERSHIP_HPP

// Copyright (c) 2026 29thnight. Licensed under the MIT License; see LICENSE.
#include <atomic>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <typeindex> // declares std::hash for the object_id specialization; <functional> costs 3x the parse time
#include <new>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// Configuration. Every macro below must have the same value in every
// translation unit of a program, except OWN_DEBUG_THREAD_CHECK (see README).
// On MSVC and clang-cl, #pragma detect_mismatch turns a disagreement inside
// one linked binary (an .exe or one .dll) into a link error.
// ---------------------------------------------------------------------------

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

// MSVC accepts and ignores the standard attribute; its own spelling has the
// layout effect. Without it the compact control blocks grow by a padding step.
#if defined(_MSC_VER) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(msvc::no_unique_address)
#define OWN_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#endif
#endif
#ifndef OWN_NO_UNIQUE_ADDRESS
#define OWN_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// Clang's lifetime analysis (-Wdangling, -Wreturn-stack-address) uses these
// to report a borrowed view that outlives the owner it came from, such as
// `return local_owner.borrow();`. Other compilers ignore them.
#if defined(__clang__)
#define OWN_GSL_OWNER(T) [[gsl::Owner(T)]]
#define OWN_GSL_POINTER(T) [[gsl::Pointer(T)]]
#define OWN_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define OWN_GSL_OWNER(T)
#define OWN_GSL_POINTER(T)
#define OWN_LIFETIMEBOUND
#endif

// Thread identity for the debug thread checks of local_owner. By default each
// binary keeps its own counter. A program whose local owners cross shared
// library / DLL boundaries must give every module one provider, or the same
// thread receives a different ID in each module and the check aborts falsely:
//
//   // every module, before including this header (e.g. in a wrapper header):
//   #define OWN_THREAD_ID_PROVIDER engine_own_thread_id
//   #define OWN_THREAD_ID_PROVIDER_API __declspec(dllimport) // dllexport in the defining module
//   // exactly one source file of exactly one module:
//   OWN_DEFINE_THREAD_ID_PROVIDER()
//
// The provider must be noexcept, callable on every thread, and return a value
// that is nonzero and never given to another thread (zero disables checks).
#ifndef OWN_THREAD_ID_PROVIDER_API
#define OWN_THREAD_ID_PROVIDER_API
#endif
#ifdef OWN_THREAD_ID_PROVIDER
extern "C" OWN_THREAD_ID_PROVIDER_API std::size_t OWN_THREAD_ID_PROVIDER() noexcept;
#define OWN_DEFINE_THREAD_ID_PROVIDER()                                                             \
    extern "C" OWN_THREAD_ID_PROVIDER_API std::size_t OWN_THREAD_ID_PROVIDER() noexcept             \
    {                                                                                               \
        return ::own::detail::monotonic_thread_id();                                                \
    }
#endif

#define OWN_DETAIL_STRINGIFY_VALUE(value) #value
#define OWN_DETAIL_STRINGIFY(value) OWN_DETAIL_STRINGIFY_VALUE(value)
#if defined(_MSC_VER)
#pragma detect_mismatch("own_enable_unsafe_get_warning", OWN_DETAIL_STRINGIFY(OWN_ENABLE_UNSAFE_GET_WARNING))
#ifdef OWN_THREAD_ID_PROVIDER
#pragma detect_mismatch("own_thread_id_provider", OWN_DETAIL_STRINGIFY(OWN_THREAD_ID_PROVIDER))
#else
#pragma detect_mismatch("own_thread_id_provider", "per-module")
#endif
#ifdef OWN_FAIL_FAST
#pragma detect_mismatch("own_fail_fast", "custom")
#else
#pragma detect_mismatch("own_fail_fast", "abort")
#endif
#endif

namespace own
{
    // Why a contract violation stopped the program. Passed to OWN_FAIL_FAST.
    enum class failure : unsigned char
    {
        use_after_release,   // a reference was added to an object whose count was zero
        count_overflow,      // more than 2^31 strong/weak references, or a local alias count overflow
        thread_mismatch,     // a local_owner group was used off its originating thread (debug checks)
        double_registration, // enable_owner_from_this was registered twice
        invalid_allocator,   // an allocator_ref is missing an allocate or deallocate callback
        thread_ids_exhausted // the default thread-ID counter wrapped
    };

    // Stable text for logs and crash reports.
    constexpr const char* to_string(failure reason) noexcept
    {
        switch (reason)
        {
        case failure::use_after_release: return "own: reference added after release";
        case failure::count_overflow: return "own: reference count overflow";
        case failure::thread_mismatch: return "own: local_owner used off its originating thread";
        case failure::double_registration: return "own: enable_owner_from_this registered twice";
        case failure::invalid_allocator: return "own: allocator_ref without allocate/deallocate";
        case failure::thread_ids_exhausted: return "own: thread IDs exhausted";
        }
        return "own: unknown failure";
    }
}

// Called with an own::failure on every contract violation the library detects.
// Define before inclusion to report to an engine crash handler; it may log and
// terminate but must not return or throw (std::abort() follows it regardless).
//   #define OWN_FAIL_FAST(reason) ::engine::fatal(::own::to_string(reason))
#ifndef OWN_FAIL_FAST
#define OWN_FAIL_FAST(reason) ((void)(reason))
#endif

namespace own
{
    // The default allocation of unique_owner: typed `new T(...)` / `delete`,
    // which honors class-specific operator new/delete. Empty, so it adds no
    // bytes to the owner.
    struct new_delete {};
    struct allocator_ref;

    template<class T, class Alloc = new_delete> class unique_owner;
    template<class T> class local_owner;
    template<class T> class shared_owner;
    template<class T> class weak_owner;
    template<class T> class borrowed;
    template<class T> class enable_owner_from_this;
    class retirement_task;
    struct retirement_hook;
    class object_id;

    namespace detail
    {
#if defined(_MSC_VER)
#pragma warning(push)
// A [[noreturn]] OWN_FAIL_FAST hook makes the abort below unreachable (C4702).
#pragma warning(disable : 4702)
#endif
        [[noreturn]] OWN_NOINLINE inline void fail_fast(failure reason) noexcept
        {
            OWN_FAIL_FAST(reason);
            std::abort();
        }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

        inline void* default_allocate(void*, std::size_t bytes, std::size_t alignment)
        {
            if (alignment > alignof(std::max_align_t))
            {
                return ::operator new(bytes, std::align_val_t(alignment));
            }
            return ::operator new(bytes);
        }

        // Every caller passes the exact size it allocated, so the sized forms
        // apply: size-class allocators (mimalloc, jemalloc, tcmalloc, engine
        // allocators replacing global new) then skip looking the size up. A
        // library or allocator without sized support forwards to the unsized form.
        // (Clang before 19 leaves sized deallocation off unless asked with
        // -fsized-deallocation; the unsized forms are used there.)
        inline void default_deallocate(void*, void* pointer, [[maybe_unused]] std::size_t bytes,
                                       std::size_t alignment) noexcept
        {
#if defined(__cpp_sized_deallocation)
            if (alignment > alignof(std::max_align_t))
            {
                ::operator delete(pointer, bytes, std::align_val_t(alignment));
                return;
            }
            ::operator delete(pointer, bytes);
#else
            if (alignment > alignof(std::max_align_t))
            {
                ::operator delete(pointer, std::align_val_t(alignment));
                return;
            }
            ::operator delete(pointer);
#endif
        }

        template<class From, class To, class Alloc = new_delete>
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
            else if constexpr (std::is_same_v<Alloc, new_delete>)
            {
                // Unlike an erased owner, the default deletes through its view
                // type. A polymorphic upcast must preserve valid destruction.
                return std::has_virtual_destructor_v<To> && std::is_nothrow_destructible_v<To>;
            }
            else
            {
                // A user allocator is handed sizeof/alignof of the owned type
                // on release; through a base that would be the wrong size.
                return false;
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

        // True when converting From* to To* never reads the object: the same
        // class, or a non-virtual base (a fixed offset). A downcast static_cast
        // is well-formed exactly when the base is accessible, unambiguous and
        // not virtual. Only a virtual-base conversion reads the object, which
        // [basic.life] forbids once its lifetime has ended; a non-virtual one
        // is valid while the storage exists, as it does under a weak reference.
        template<class From, class To>
        inline constexpr bool fixed_offset_conversion =
            std::is_same_v<std::remove_cv_t<From>, std::remove_cv_t<To>> ||
            requires(std::remove_cv_t<To>* base) { static_cast<std::remove_cv_t<From>*>(base); };

        struct owner_registration_marker {};
        struct control_block;
        struct owner_access;
        void last_strong_released(control_block*) noexcept;
        void retire_through_hook(control_block*, const retirement_hook&) noexcept;
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
        friend void detail::retire_through_hook(detail::control_block*, const retirement_hook&) noexcept;
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
            // The whole last-strong release for one block type, so its payload
            // destructor and deallocation inline into a single indirect call.
            void (*release)(control_block*) noexcept;
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
            if (previous == 0 || previous >= saturation_limit) [[unlikely]]
            {
                fail_fast(previous == 0 ? failure::use_after_release : failure::count_overflow);
            }
        }
        inline void add_weak(control_block* block) noexcept
        {
            const auto previous = weak_of(block->counts.fetch_add(weak_one, std::memory_order_relaxed));
            if (previous == 0 || previous >= saturation_limit) [[unlikely]]
            {
                fail_fast(previous == 0 ? failure::use_after_release : failure::count_overflow);
            }
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
            block->ops->release(block);
        }

        // A hook receives the task; a task it drops runs immediately.
        inline void retire_through_hook(control_block* block, const retirement_hook& hook) noexcept
        {
            hook.retire(hook.context, retirement_task(block));
        }

        // The typed body of control_ops::release: dispose, then drop the
        // implicit weak reference, with no indirect call for either step.
        template<class Block>
        void release_block(control_block* base) noexcept
        {
            if constexpr (Block::has_retirement_hook)
            {
                const retirement_hook& hook = static_cast<Block*>(base)->retirement;
                if (hook.retire)
                {
                    retire_through_hook(base, hook);
                    return;
                }
            }
            Block::dispose_object(base);
            // As release_weak: no other reference means no read-modify-write.
            if (base->counts.load(std::memory_order_acquire) == weak_one ||
                weak_of(base->counts.fetch_sub(weak_one, std::memory_order_acq_rel)) == 1)
            {
                Block::destroy_control(base);
            }
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
                if (strong_of(value) >= saturation_limit) { fail_fast(failure::count_overflow); }
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
            if (!allocator.allocate || !allocator.deallocate) { fail_fast(failure::invalid_allocator); }
            void* result = allocator.allocate(allocator.context, size, alignment);
            if (!result) { throw std::bad_alloc(); }
            return result;
        }

        // The default identity, and what OWN_DEFINE_THREAD_ID_PROVIDER exports.
        // Its counter is an inline static, so each module that does not share
        // one provider has its own: see OWN_THREAD_ID_PROVIDER.
        inline std::size_t monotonic_thread_id() noexcept
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
                    if (value == count_limit) { fail_fast(failure::thread_ids_exhausted); }
                    if (next.compare_exchange_weak(value, value + 1,
                            std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        return value;
                    }
                }
            }();
            return id;
        }

        inline std::size_t current_thread_id() noexcept
        {
#ifdef OWN_THREAD_ID_PROVIDER
            return ::OWN_THREAD_ID_PROVIDER();
#else
            return monotonic_thread_id();
#endif
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
                if (thread_id != 0 && thread_id != current_thread_id()) { fail_fast(failure::thread_mismatch); }
#endif
            }
            void add_reference() noexcept
            {
                check_thread();
                if (references >= count_limit - exclusive_bit) { fail_fast(failure::count_overflow); }
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
                                             release_block<Block>};

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
            OWN_NO_UNIQUE_ADDRESS std::conditional_t<Group, group_slot, no_group_slot> group;
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
            static constexpr bool has_retirement_hook = false;
        };

        // allocate_* and *_with factories: carries the deallocation callback and
        // retirement hook. The allocate callback is not needed after allocation.
        template<class T, bool Group = false>
        struct allocated_control final : control_block
        {
            void* context;
            deallocate_function deallocate;
            retirement_hook retirement;
            OWN_NO_UNIQUE_ADDRESS std::conditional_t<Group, group_slot, no_group_slot> group;
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
            static constexpr bool has_retirement_hook = true;
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
    class OWN_GSL_POINTER(T) borrowed
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        constexpr borrowed() noexcept = default;
        constexpr borrowed(std::nullptr_t) noexcept {}
        template<class U> requires std::is_convertible_v<U*, T*>
        constexpr borrowed(const borrowed<U>& other) noexcept : pointer_(other.pointer_) {}
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        constexpr T* unsafe_get() const noexcept { return pointer_; }
        constexpr explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend constexpr bool operator==(const borrowed& handle, std::nullptr_t) noexcept { return !handle; }
        constexpr T& operator*() const noexcept { return *pointer_; }
        constexpr T* operator->() const noexcept { return pointer_; }
        constexpr void reset() noexcept { pointer_ = nullptr; }
        constexpr void swap(borrowed& other) noexcept { std::swap(pointer_, other.pointer_); }

    private:
        T* pointer_ = nullptr;
        explicit constexpr borrowed(T* pointer) noexcept : pointer_(pointer) {}
        template<class> friend class borrowed;
        template<class, class> friend class unique_owner;
        template<class> friend class shared_owner;
        template<class> friend class local_owner;
        template<class> friend class enable_owner_from_this;
    };

    // The earlier name of borrowed<T>. An exact alias, so existing code and
    // overloads keep working; "local" now names only thread confinement.
    template<class T> using local_view = borrowed<T>;

    // The allocator interface of unique_owner<T, Alloc>. An allocator derives
    // from it publicly, naming itself (CRTP), and implements two members:
    //
    //   struct frame_pool_ref : own::allocator_interface<frame_pool_ref>
    //   {
    //       frame_pool* pool;
    //       void* do_allocate(std::size_t size, std::size_t alignment);
    //       void do_deallocate(void* pointer, std::size_t size, std::size_t alignment) noexcept;
    //   };
    //
    // Owners call only the interface's allocate/deallocate, which forward to
    // them. The contract matches allocator_ref: deallocate receives exactly the
    // size and alignment that were allocated and runs on the releasing thread;
    // do_allocate may return null (reported as std::bad_alloc) or throw.
    //
    // The interface is static, not virtual: it adds no vtable pointer, so an
    // allocator with no data members stays empty and its owner one pointer.
    template<class Derived>
    class allocator_interface
    {
    public:
        [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment)
        {
            void* storage = self().do_allocate(size, alignment);
            if (!storage) { throw std::bad_alloc(); }
            return storage;
        }
        void deallocate(void* storage, std::size_t size, std::size_t alignment) noexcept
        {
            self().do_deallocate(storage, size, alignment);
        }

        // Special members stay public and trivial so an allocator can be a
        // plain aggregate: `frame_pool_ref{.pool = &pool}`.

    private:
        Derived& self() noexcept
        {
            static_assert(std::derived_from<Derived, allocator_interface>,
                          "allocator_interface<Derived> must be a public base of Derived");
            return static_cast<Derived&>(*this);
        }
    };

    // What unique_owner<T, Alloc> requires of Alloc, checked at the point of use:
    //  - a public, unambiguous allocator_interface<Alloc> base
    //  - void* do_allocate(size_t, size_t) and a noexcept
    //    do_deallocate(void*, size_t, size_t), callable by the interface
    //  - noexcept copy, move and destruction (owners copy it as they move)
    template<class A>
    concept unique_allocator =
        std::is_class_v<A> && std::derived_from<A, allocator_interface<A>> &&
        std::is_nothrow_copy_constructible_v<A> && std::is_nothrow_move_constructible_v<A> &&
        std::is_nothrow_copy_assignable_v<A> && std::is_nothrow_move_assignable_v<A> &&
        std::is_nothrow_destructible_v<A> &&
        requires(A& allocator, void* pointer, std::size_t bytes) {
            { allocator.do_allocate(bytes, bytes) } -> std::same_as<void*>;
            { allocator.do_deallocate(pointer, bytes, bytes) } noexcept;
        };

    // Exclusive ownership. The allocation is part of the type:
    //  - unique_owner<T> (new_delete): one pointer. Typed new/delete supply
    //    destruction and allocation lookup without runtime metadata.
    //  - unique_owner<T, Alloc> for an allocator_interface implementation: the pointer plus the
    //    allocator object (nothing more when Alloc is empty). Releases exactly
    //    sizeof(T)/alignof(T), so it converts only to a differently
    //    cv-qualified T, never to a base.
    //  - unique_owner<T, allocator_ref>: a runtime allocator chosen per object;
    //    see its specialization below.
    template<class T, class Alloc>
    class OWN_GSL_OWNER(T) unique_owner
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_same_v<Alloc, new_delete> || unique_allocator<Alloc>,
                      "unique_owner's second argument must be own::new_delete, own::allocator_ref, or an "
                      "allocator deriving from own::allocator_interface<Alloc> with void* do_allocate(size_t, "
                      "size_t) and a noexcept do_deallocate(void*, size_t, size_t)");
        static constexpr bool default_allocation = std::is_same_v<Alloc, new_delete>;
    public:
        using element_type = T;
        using allocator_type = Alloc;
        unique_owner() noexcept requires std::is_default_constructible_v<Alloc> = default;
        unique_owner(std::nullptr_t) noexcept requires std::is_default_constructible_v<Alloc> {}
        // Constructs a new T in place: `unique_owner<Mesh> mesh(std::in_place, args...)`.
        // The tag keeps "make a new object" distinct from adopting an existing
        // one, which no constructor does. Stateful allocators have no default
        // instance, so they are passed instead of the tag.
        template<class... Args>
            requires std::is_empty_v<Alloc> && std::is_default_constructible_v<Alloc> &&
                     std::is_constructible_v<T, Args...>
        explicit unique_owner(std::in_place_t, Args&&... args)
            : pointer_(create(alloc_, std::forward<Args>(args)...))
        {
        }
        // Constructs a new T with storage from `allocator`, which this owner
        // keeps and releases the storage to: `unique_owner<Mesh, FramePool> mesh(pool, args...)`.
        template<class... Args>
            requires (!default_allocation) && std::is_constructible_v<T, Args...>
        explicit unique_owner(const Alloc& allocator, Args&&... args)
            : alloc_(allocator), pointer_(create(alloc_, std::forward<Args>(args)...))
        {
        }
        unique_owner(const unique_owner&) = delete;
        unique_owner& operator=(const unique_owner&) = delete;
        unique_owner(unique_owner&& other) noexcept
            : alloc_(other.alloc_), pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        template<class U> requires detail::unique_conversion_allowed<U, T, Alloc>
        unique_owner(unique_owner<U, Alloc>&& other) noexcept
            : alloc_(other.alloc_), pointer_(std::exchange(other.pointer_, nullptr))
        {
        }
        ~unique_owner() { destroy(alloc_, pointer_); }
        unique_owner& operator=(unique_owner&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        template<class U> requires detail::unique_conversion_allowed<U, T, Alloc>
        unique_owner& operator=(unique_owner<U, Alloc>&& other) noexcept
        {
            unique_owner(std::move(other)).swap(*this);
            return *this;
        }
        void reset() noexcept { destroy(alloc_, std::exchange(pointer_, nullptr)); }
        void swap(unique_owner& other) noexcept
        {
            std::swap(pointer_, other.pointer_);
            if constexpr (!std::is_empty_v<Alloc>) { std::swap(alloc_, other.alloc_); }
        }
        // The allocator this owner releases to (a copy).
        Alloc get_allocator() const noexcept { return alloc_; }
#if OWN_ENABLE_UNSAFE_GET_WARNING
        [[deprecated("Borrowed raw pointer: caller must preserve lifetime; do not delete or otherwise deallocate the returned pointer")]]
#endif
        T* unsafe_get() const & noexcept { return pointer_; }
        T* unsafe_get() const && = delete;
        [[nodiscard]] borrowed<T> borrow() const & noexcept OWN_LIFETIMEBOUND { return borrowed<T>(pointer_); }
        borrowed<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend bool operator==(const unique_owner& handle, std::nullptr_t) noexcept { return !handle; }
        T& operator*() const noexcept { return *pointer_; }
        T* operator->() const noexcept { return pointer_; }

    private:
        // The allocator first: an empty one then occupies no storage, and a
        // stateful one is initialized before it allocates the object.
        OWN_NO_UNIQUE_ADDRESS Alloc alloc_{};
        T* pointer_ = nullptr;

        template<class... Args>
        static T* create(Alloc& allocator, Args&&... args)
        {
            static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
            if constexpr (default_allocation)
            {
                (void)allocator;
                // Ordinary typed new/delete honor class-specific allocation
                // functions, including language-provided constructor-failure cleanup.
                return new T(std::forward<Args>(args)...);
            }
            else
            {
                // Through the interface: a null result is already std::bad_alloc.
                void* storage = allocator.allocate(sizeof(T), alignof(T));
                try
                {
                    return ::new (storage) T(std::forward<Args>(args)...);
                }
                catch (...)
                {
                    allocator.deallocate(storage, sizeof(T), alignof(T));
                    throw;
                }
            }
        }
        static void destroy(Alloc& allocator, T* pointer) noexcept
        {
            static_assert(sizeof(T) > 0, "unique_owner destruction requires a complete payload type");
            static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be accessible and noexcept");
            if (!pointer) { return; }
            if constexpr (default_allocation)
            {
                (void)allocator;
                delete pointer;
            }
            else
            {
                void* storage = const_cast<void*>(static_cast<const volatile void*>(pointer));
                pointer->~T();
                allocator.deallocate(storage, sizeof(T), alignof(T));
            }
        }
        unique_owner(const Alloc& allocator, T* pointer, std::nullptr_t) noexcept
            : alloc_(allocator), pointer_(pointer)
        {
        }
        explicit unique_owner(T* pointer) noexcept requires default_allocation : pointer_(pointer) {}
        template<class, class> friend class unique_owner;
        friend struct detail::owner_access;
    };

    // Runtime allocator chosen per object (formerly allocated_unique_owner).
    // The handle keeps the access pointer, the original allocation, the
    // allocator context and callback, and the concrete type's disposer: five
    // words, no second allocation or count. The preserved disposer makes every
    // safe base conversion valid, virtual destructor or not.
    template<class T>
    class OWN_GSL_OWNER(T) unique_owner<T, allocator_ref>
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
    public:
        using element_type = T;
        using allocator_type = allocator_ref;
        unique_owner() noexcept = default;
        unique_owner(std::nullptr_t) noexcept {}
        // Constructs a new T with the default allocator_ref (global operator new).
        template<class... Args> requires std::is_constructible_v<T, Args...>
        explicit unique_owner(std::in_place_t, Args&&... args)
        {
            emplace_from(allocator_ref{}, std::forward<Args>(args)...);
        }
        // Constructs a new T with storage from `allocator`.
        template<class... Args> requires std::is_constructible_v<T, Args...>
        explicit unique_owner(allocator_ref allocator, Args&&... args)
        {
            emplace_from(allocator, std::forward<Args>(args)...);
        }
        unique_owner(const unique_owner&) = delete;
        unique_owner& operator=(const unique_owner&) = delete;
        unique_owner(unique_owner&& other) noexcept { take_from(other); }
        template<class U> requires std::is_convertible_v<U*, T*>
        unique_owner(unique_owner<U, allocator_ref>&& other) noexcept { take_from(other); }
        ~unique_owner() { reset(); }
        unique_owner& operator=(unique_owner&& other) noexcept
        {
            move_assign(other);
            return *this;
        }
        template<class U> requires std::is_convertible_v<U*, T*>
        unique_owner& operator=(unique_owner<U, allocator_ref>&& other) noexcept
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
        void swap(unique_owner& other) noexcept
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
        [[nodiscard]] borrowed<T> borrow() const & noexcept OWN_LIFETIMEBOUND { return borrowed<T>(pointer_); }
        borrowed<T> borrow() const && = delete;
        explicit operator bool() const noexcept { return pointer_ != nullptr; }
        // Null comparison only; owners/views are never compared with each other or ordered.
        friend bool operator==(const unique_owner& handle, std::nullptr_t) noexcept { return !handle; }
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

        unique_owner(T* pointer, void* storage, allocator_ref allocator,
                     detail::unique_dispose_function dispose) noexcept;
        template<class... Args>
        void emplace_from(allocator_ref allocator, Args&&... args);
        // Same observable order as constructing a temporary and swapping: this
        // handle holds the new object before the old one is disposed, so cleanup
        // may reassign it. Avoids the temporary and swap's emptiness branches.
        template<class U>
        void move_assign(unique_owner<U, allocator_ref>& other) noexcept
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
        void take_from(unique_owner<U, allocator_ref>& other) noexcept
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
        template<class, class> friend class unique_owner;
        friend struct detail::owner_access;
        template<class U, class... Args>
        friend unique_owner<U, allocator_ref> allocate_unique(allocator_ref, Args&&...);
    };

    // The earlier name of the runtime-allocator owner.
    template<class T>
    using allocated_unique_owner [[deprecated("use own::unique_owner<T, own::allocator_ref>")]] =
        unique_owner<T, allocator_ref>;

    template<class T>
    class OWN_GSL_OWNER(T) shared_owner
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
        [[nodiscard]] borrowed<T> borrow() const & noexcept OWN_LIFETIMEBOUND { return borrowed<T>(pointer_); }
        borrowed<T> borrow() const && = delete;
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
    class OWN_GSL_OWNER(T) local_owner
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
        [[nodiscard]] borrowed<T> borrow() const & noexcept OWN_LIFETIMEBOUND
        {
            check_thread();
            return borrowed<T>(pointer_);
        }
        borrowed<T> borrow() const && = delete;
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
                if constexpr (detail::fixed_offset_conversion<U, T>)
                {
                    // A fixed offset reads nothing: no strong reference needed.
                    pointer_ = other.pointer_;
                }
                // Derived-to-virtual-base adjustment can read the object.
                // Never adjust an expired pointer, even though its storage
                // remains allocated for the weak control block.
                else if (detail::try_add_strong(block_))
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
            : block_(std::exchange(other.block_, nullptr))
        {
            // Takes over the source's weak reference instead of adding one
            // and releasing the other. The pointer is adjusted only while a
            // temporary strong reference proves the object is alive.
            U* const source = std::exchange(other.pointer_, nullptr);
            if constexpr (detail::fixed_offset_conversion<U, T>)
            {
                pointer_ = source;
            }
            else if (block_ && detail::try_add_strong(block_))
            {
                pointer_ = source;
                detail::release_strong(block_);
            }
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
        friend struct detail::owner_access;
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
        [[nodiscard]] borrowed<T> borrow_from_this() & noexcept
        {
            return weak_.expired() ? borrowed<T>() : borrowed<T>(weak_.pointer_);
        }
        [[nodiscard]] borrowed<const T> borrow_from_this() const & noexcept
        {
            return weak_.expired() ? borrowed<const T>() : borrowed<const T>(weak_.pointer_);
        }
        borrowed<T> borrow_from_this() && = delete;
        borrowed<const T> borrow_from_this() const && = delete;
        // Synonyms kept for compatibility. "local" names thread confinement
        // (local_owner); these return an unchecked view, so prefer borrow_from_this().
        [[deprecated("use borrow_from_this(); local_from_this() returns the same borrowed view")]]
        [[nodiscard]] borrowed<T> local_from_this() & noexcept { return borrow_from_this(); }
        [[deprecated("use borrow_from_this(); local_from_this() returns the same borrowed view")]]
        [[nodiscard]] borrowed<const T> local_from_this() const & noexcept { return borrow_from_this(); }
        borrowed<T> local_from_this() && = delete;
        borrowed<const T> local_from_this() const && = delete;

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
            if (weak_.block_) { detail::fail_fast(failure::double_registration); }
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

            // ---- Casts. A failed cast returns empty and leaves the source as it was.
            template<class To, class From, class Cast>
            static shared_owner<To> cast_shared(const shared_owner<From>& source, Cast cast) noexcept
            {
                if (!source.bits_) { return {}; }
                To* const pointer = cast(source.pointer_);
                if (!pointer) { return {}; }
                add_strong(source.block());
                return shared_owner<To>(source.block(), pointer); // a copy: no fresh hint
            }
            template<class To, class From, class Cast>
            static shared_owner<To> cast_shared(shared_owner<From>&& source, Cast cast) noexcept
            {
                if (!source.bits_) { return {}; }
                To* const pointer = cast(source.pointer_);
                if (!pointer) { return {}; }
                shared_owner<To> result;
                result.bits_ = std::exchange(source.bits_, 0); // keeps the fresh hint, as a move does
                result.pointer_ = pointer;
                source.pointer_ = nullptr;
                return result;
            }
            template<class To, class From, class Cast>
            static local_owner<To> cast_local(const local_owner<From>& source, Cast cast) noexcept
            {
                source.check_thread();
                if (!source.group_) { return {}; }
                To* const pointer = cast(source.pointer_);
                if (!pointer) { return {}; }
                source.group_->add_reference();
                return local_owner<To>(source.group_, pointer);
            }
            template<class To, class From, class Cast>
            static local_owner<To> cast_local(local_owner<From>&& source, Cast cast) noexcept
            {
                source.check_thread();
                if (!source.group_) { return {}; }
                To* const pointer = cast(source.pointer_);
                if (!pointer) { return {}; }
                source.pointer_ = nullptr;
                return local_owner<To>(std::exchange(source.group_, nullptr), pointer);
            }
            template<class To, class From>
            static unique_owner<To> cast_unique(unique_owner<From>&& source) noexcept
            {
                To* const pointer = source.pointer_ ? dynamic_cast<To*>(source.pointer_) : nullptr;
                if (!pointer) { return {}; }
                source.pointer_ = nullptr;
                return unique_owner<To>(pointer);
            }
            template<class To, class From>
            static unique_owner<To, allocator_ref> cast_allocated_unique(unique_owner<From, allocator_ref>&& source) noexcept
            {
                To* const pointer = source.pointer_ ? dynamic_cast<To*>(source.pointer_) : nullptr;
                if (!pointer) { return {}; }
                // Storage, allocator and the concrete disposer stay with the object.
                unique_owner<To, allocator_ref> result;
                result.pointer_ = pointer;
                result.storage_ = source.storage_;
                result.context_ = source.context_;
                result.deallocate_ = source.deallocate_;
                result.dispose_ = source.dispose_;
                source.pointer_ = nullptr;
                return result;
            }

            // ---- Identity.
            static std::uintptr_t address(const volatile void* pointer) noexcept
            {
                return reinterpret_cast<std::uintptr_t>(pointer);
            }
            template<class T>
            static const volatile void* complete_object(T* pointer) noexcept
            {
                if constexpr (std::is_polymorphic_v<T>) { return dynamic_cast<const volatile void*>(pointer); }
                else { return pointer; }
            }
            template<class T>
            static std::uintptr_t identity_of(const shared_owner<T>& owner) noexcept
            {
                return address(owner.block());
            }
            template<class T>
            static std::uintptr_t identity_of(const weak_owner<T>& owner) noexcept
            {
                return address(owner.block_);
            }
            template<class T>
            static std::uintptr_t identity_of(const local_owner<T>& owner) noexcept
            {
                owner.check_thread();
                return owner.group_ ? address(owner.group_->block) : 0;
            }
            template<class T, class Alloc>
            static std::uintptr_t identity_of(const unique_owner<T, Alloc>& owner) noexcept
            {
                return owner.pointer_ ? address(complete_object(owner.pointer_)) : 0;
            }
            template<class T>
            static std::uintptr_t identity_of(const unique_owner<T, allocator_ref>& owner) noexcept
            {
                return owner.pointer_ ? address(owner.storage_) : 0;
            }
            static object_id make_id(std::uintptr_t value) noexcept;
        };
    }

    template<class T>
    unique_owner<T, allocator_ref>::unique_owner(T* pointer, void* storage, allocator_ref allocator,
                                                 detail::unique_dispose_function dispose) noexcept
        : pointer_(pointer), storage_(storage), context_(allocator.context),
          deallocate_(allocator.deallocate), dispose_(dispose)
    {
    }

    template<class T>
    template<class... Args>
    void unique_owner<T, allocator_ref>::emplace_from(allocator_ref allocator, Args&&... args)
    {
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
        pointer_ = pointer;
        storage_ = storage;
        context_ = allocator.context;
        deallocate_ = allocator.deallocate;
        dispose_ = detail::dispose_unique<T>;
    }

    // Runtime allocator: returns unique_owner<T, allocator_ref>.
    template<class T, class... Args>
    [[nodiscard]] unique_owner<T, allocator_ref> allocate_unique(allocator_ref allocator, Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
        unique_owner<T, allocator_ref> owner;
        owner.emplace_from(allocator, std::forward<Args>(args)...);
        return owner;
    }

    // Allocator type: returns unique_owner<T, Alloc>, which keeps `allocator`.
    template<class T, unique_allocator Alloc, class... Args>
    [[nodiscard]] unique_owner<T, Alloc> allocate_unique(const Alloc& allocator, Args&&... args)
    {
        static_assert(std::is_object_v<T> && !std::is_array_v<T>);
        static_assert(std::is_nothrow_destructible_v<T>, "owned destructors must be noexcept");
        return unique_owner<T, Alloc>(allocator, std::forward<Args>(args)...);
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

    // ---------------------------------------------------------------------
    // Owner casts. Like the std pointer casts, but a failed dynamic cast
    // returns an empty owner and leaves an rvalue source untouched, so the
    // caller still owns the object. Const qualification is never removed.
    // ---------------------------------------------------------------------

    template<class To, class From>
        requires requires(From* pointer) { static_cast<To*>(pointer); }
    [[nodiscard]] shared_owner<To> static_owner_cast(const shared_owner<From>& source) noexcept
    {
        return detail::owner_access::cast_shared<To>(source, [](From* p) noexcept { return static_cast<To*>(p); });
    }
    template<class To, class From>
        requires requires(From* pointer) { static_cast<To*>(pointer); }
    [[nodiscard]] shared_owner<To> static_owner_cast(shared_owner<From>&& source) noexcept
    {
        return detail::owner_access::cast_shared<To>(std::move(source), [](From* p) noexcept { return static_cast<To*>(p); });
    }
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); }
    [[nodiscard]] shared_owner<To> dynamic_owner_cast(const shared_owner<From>& source) noexcept
    {
        return detail::owner_access::cast_shared<To>(source, [](From* p) noexcept { return dynamic_cast<To*>(p); });
    }
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); }
    [[nodiscard]] shared_owner<To> dynamic_owner_cast(shared_owner<From>&& source) noexcept
    {
        return detail::owner_access::cast_shared<To>(std::move(source), [](From* p) noexcept { return dynamic_cast<To*>(p); });
    }

    template<class To, class From>
        requires requires(From* pointer) { static_cast<To*>(pointer); }
    [[nodiscard]] local_owner<To> static_owner_cast(const local_owner<From>& source) noexcept
    {
        return detail::owner_access::cast_local<To>(source, [](From* p) noexcept { return static_cast<To*>(p); });
    }
    template<class To, class From>
        requires requires(From* pointer) { static_cast<To*>(pointer); }
    [[nodiscard]] local_owner<To> static_owner_cast(local_owner<From>&& source) noexcept
    {
        return detail::owner_access::cast_local<To>(std::move(source), [](From* p) noexcept { return static_cast<To*>(p); });
    }
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); }
    [[nodiscard]] local_owner<To> dynamic_owner_cast(const local_owner<From>& source) noexcept
    {
        return detail::owner_access::cast_local<To>(source, [](From* p) noexcept { return dynamic_cast<To*>(p); });
    }
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); }
    [[nodiscard]] local_owner<To> dynamic_owner_cast(local_owner<From>&& source) noexcept
    {
        return detail::owner_access::cast_local<To>(std::move(source), [](From* p) noexcept { return dynamic_cast<To*>(p); });
    }

    // Exclusive owners: checked downcasts only, from an rvalue. unique_owner<T>
    // deletes through the result type, so that type must have a virtual
    // destructor (or be the source type itself). unique_owner<T, allocator_ref>
    // keeps its concrete disposer, so any successful cast is safe to destroy.
    // unique_owner<T, Alloc> with a user allocator never holds a derived
    // object through a base, so it has no downcast.
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); } &&
                 (std::is_same_v<std::remove_cv_t<To>, std::remove_cv_t<From>> ||
                  (std::has_virtual_destructor_v<To> && std::is_nothrow_destructible_v<To>))
    [[nodiscard]] unique_owner<To> dynamic_owner_cast(unique_owner<From>&& source) noexcept
    {
        return detail::owner_access::cast_unique<To>(std::move(source));
    }
    template<class To, class From>
        requires std::is_polymorphic_v<From> && requires(From* pointer) { dynamic_cast<To*>(pointer); }
    [[nodiscard]] unique_owner<To, allocator_ref> dynamic_owner_cast(unique_owner<From, allocator_ref>&& source) noexcept
    {
        return detail::owner_access::cast_allocated_unique<To>(std::move(source));
    }

    // ---------------------------------------------------------------------
    // Identity. Owners still have no ==, < or hash of their own; identity()
    // gives an opaque key that does not expose a dereferenceable pointer.
    //  - shared_owner, weak_owner and local_owner: the managed object's control
    //    block, so every strong, weak and local handle of one object and every
    //    cast of it compare equal, and a weak key stays valid after expiry.
    //  - unique_owner (any allocator): the owned allocation.
    // A key is unique among objects whose control block (or allocation) is
    // still alive; afterwards the address, and so the key, may be reused.
    // Empty handles give the null key.
    // ---------------------------------------------------------------------
    class object_id
    {
    public:
        constexpr object_id() noexcept = default;
        constexpr explicit operator bool() const noexcept { return value_ != 0; }
        friend constexpr bool operator==(const object_id&, const object_id&) noexcept = default;
        friend constexpr std::strong_ordering operator<=>(const object_id&, const object_id&) noexcept = default;
        std::size_t hash() const noexcept
        {
            // Addresses share their low bits; mix before bucketing.
            std::uint64_t x = static_cast<std::uint64_t>(value_);
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return static_cast<std::size_t>(x);
        }

    private:
        std::uintptr_t value_ = 0;
        friend struct detail::owner_access;
    };

    inline object_id detail::owner_access::make_id(std::uintptr_t value) noexcept
    {
        object_id id;
        id.value_ = value;
        return id;
    }

    template<class T>
    [[nodiscard]] object_id identity(const shared_owner<T>& owner) noexcept
    {
        return detail::owner_access::make_id(detail::owner_access::identity_of(owner));
    }
    template<class T>
    [[nodiscard]] object_id identity(const weak_owner<T>& owner) noexcept
    {
        return detail::owner_access::make_id(detail::owner_access::identity_of(owner));
    }
    template<class T>
    [[nodiscard]] object_id identity(const local_owner<T>& owner) noexcept
    {
        return detail::owner_access::make_id(detail::owner_access::identity_of(owner));
    }
    template<class T, class Alloc>
    [[nodiscard]] object_id identity(const unique_owner<T, Alloc>& owner) noexcept
    {
        return detail::owner_access::make_id(detail::owner_access::identity_of(owner));
    }

    template<class T, class Alloc> void swap(unique_owner<T, Alloc>& a, unique_owner<T, Alloc>& b) noexcept { a.swap(b); }
    template<class T> void swap(borrowed<T>& a, borrowed<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(shared_owner<T>& a, shared_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(local_owner<T>& a, local_owner<T>& b) noexcept { a.swap(b); }
    template<class T> void swap(weak_owner<T>& a, weak_owner<T>& b) noexcept { a.swap(b); }
}

namespace std
{
    template<>
    struct hash<own::object_id>
    {
        std::size_t operator()(const own::object_id& id) const noexcept { return id.hash(); }
    };
}

#endif
