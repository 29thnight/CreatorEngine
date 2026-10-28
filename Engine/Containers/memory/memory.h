#pragma once
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#define CE_NOINLINE __declspec(noinline)
#define CE_FORCEINLINE __forceinline
#else
#define CE_NOINLINE __attribute__((noinline))
#define CE_FORCEINLINE inline __attribute__((always_inline))
#endif
#if defined(_MSC_VER) && defined(__clang__)
#define CE_NO_UNIQUE_ADDRESS
#elif defined(_MSC_VER)
#define CE_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define CE_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
namespace ce
{
    struct bounds_error
    {
    };

    struct invalid_handle
    {
    };

    namespace detail
    {
#if defined(_MSC_VER) && defined(_WIN64)
        // Microsoft CRT documents 16-byte malloc alignment on 64-bit targets. This
        // bound must not come from a user-overridable C++ operator-new alignment flag.
        inline constexpr size_t malloc_alignment = 16;
#else
        inline constexpr size_t malloc_alignment = alignof(max_align_t);
#endif
        template<class T>
        constexpr T&& move(T& v) noexcept
        {
            return static_cast<T&&>(v);
        }
        template<class T>
        inline constexpr bool nothrow_destroy = []
        {
            if constexpr (__is_class(T) || __is_union(T))
#if defined(_MSC_VER)
                // MSVC IFC cannot reliably resolve a dependent ~T for consumer lambdas.
                // Use the same compiler trait as the runtime headers, without <type_traits>.
                return __is_nothrow_destructible(T);
#else
                return noexcept(static_cast<T*>(nullptr)->~T());
#endif
            else
                return true;
        }();
        template<class T>
        void destroy_at(T* p) noexcept
        {
            if constexpr (__is_class(T) || __is_union(T))
                p->~T();
        }
        template<class T>
        inline constexpr bool trivial = __is_trivially_copyable(T);
        template<class T>
        inline constexpr bool copyable = __is_constructible(T, const T&);
        template<class T>
        inline constexpr bool nothrow_move = __is_nothrow_constructible(T, T&&);
        template<class A, class B>
        inline constexpr bool same_type = false;
        template<class A>
        inline constexpr bool same_type<A, A> = true;
        template<class A>
        inline constexpr bool byte_reallocating_allocator = []
        {
            if constexpr (requires { typename A::byte_relocation_allocator; })
            return same_type<A, typename A::byte_relocation_allocator>&&
                requires(A & a, void* p)
            {
                a.reallocate(p, size_t{}, size_t{}, size_t{});
            };
            else return false;
        }();
        template<class T>
        inline constexpr bool nothrow_policy =
            __is_nothrow_constructible(T, const T&) && __is_nothrow_assignable(T&, const T&);
        template<class A>
        inline constexpr bool array_scoped_construction = []
        {
            if constexpr (requires { typename A::scoped_construction_allocator; })
                return same_type<A, typename A::scoped_construction_allocator>;
            else
                return false;
        }();
        template<class A>
        inline constexpr bool array_propagate_copy = []
        {
            if constexpr (requires { A::propagate_on_array_copy_assignment; })
                return A::propagate_on_array_copy_assignment;
            else
                return true;
        }();
        template<class A>
        inline constexpr bool array_propagate_move = []
        {
            if constexpr (requires { A::propagate_on_array_move_assignment; })
                return A::propagate_on_array_move_assignment;
            else
                return true;
        }();

        inline size_t checked_bytes(size_t n, size_t width)
        {
            if (width && n > SIZE_MAX / width)
                throw std::bad_alloc();
            return n * width;
        }

        inline void* heap_allocate(size_t n, size_t alignment)
        {
            if (!n)
                return nullptr;
            if (!alignment || (alignment & (alignment - 1)))
                throw std::bad_alloc();
            if (alignment > malloc_alignment)
                return ::operator new(n, std::align_val_t(alignment));
            auto p = ::malloc(n);
            if (!p)
                throw std::bad_alloc();
            return p;
        }

        inline void heap_deallocate(void* p, size_t alignment) noexcept
        {
            if (!p)
                return;
            if (alignment > malloc_alignment)
                ::operator delete(p, std::align_val_t(alignment));
            else
                ::free(p);
        }

        inline void* heap_reallocate(void* p, size_t old_bytes, size_t new_bytes, size_t alignment)
        {
            if (!alignment || (alignment & (alignment - 1)))
                throw std::bad_alloc();
            if (!new_bytes)
            {
                heap_deallocate(p, alignment);
                return nullptr;
            }
            if (alignment > malloc_alignment)
            {
                auto q = heap_allocate(new_bytes, alignment);
                if (old_bytes)
                    memcpy(q, p, old_bytes < new_bytes ? old_bytes : new_bytes);
                heap_deallocate(p, alignment);
                return q;
            }
            auto q = ::realloc(p, new_bytes);
            if (!q)
                throw std::bad_alloc();
            return q;
        }

        // Bulk copies are sensitive to 32-byte alignment on PC CPUs. Keep small CRT
        // allocations unchanged; promote large ordinary-alignment blocks portably.
        inline constexpr size_t bulk_alignment = 32, bulk_threshold = 4096;

        inline bool bulk_block(size_t bytes, size_t alignment) noexcept
        {
            return malloc_alignment < bulk_alignment && bytes >= bulk_threshold && alignment <= malloc_alignment;
        }

        inline unsigned char* bulk_pointer(void* base) noexcept
        {
            const auto address = (reinterpret_cast<uintptr_t>(base) + sizeof(void*) + bulk_alignment - 1) &
                                 ~uintptr_t(bulk_alignment - 1);
            return reinterpret_cast<unsigned char*>(address);
        }

        inline void* bulk_base(void* pointer) noexcept
        {
            void* base;
            memcpy(&base, static_cast<unsigned char*>(pointer) - sizeof(void*), sizeof(base));
            return base;
        }

        CE_NOINLINE inline void* bulk_allocate(size_t bytes)
        {
            constexpr auto extra = sizeof(void*) + bulk_alignment - 1;
            if (bytes > SIZE_MAX - extra)
                throw std::bad_alloc{};
            auto* base = ::malloc(bytes + extra);
            if (!base)
                throw std::bad_alloc{};
            auto* pointer = bulk_pointer(base);
            memcpy(pointer - sizeof(void*), &base, sizeof(base));
            return pointer;
        }

        inline void* container_allocate(size_t bytes, size_t alignment)
        {
            if (!alignment || (alignment & (alignment - 1)))
                throw std::bad_alloc{};
            return bulk_block(bytes, alignment) ? bulk_allocate(bytes) : heap_allocate(bytes, alignment);
        }

        inline void container_deallocate(void* pointer, size_t bytes, size_t alignment) noexcept
        {
            if (!pointer)
                return;
            if (bulk_block(bytes, alignment))
                ::free(bulk_base(pointer));
            else
                heap_deallocate(pointer, alignment);
        }

        CE_NOINLINE inline void* bulk_reallocate(void* pointer, size_t old_bytes, size_t new_bytes, size_t alignment)
        {
            if (!new_bytes)
            {
                container_deallocate(pointer, old_bytes, alignment);
                return nullptr;
            }
            if (!pointer)
                return container_allocate(new_bytes, alignment);
            auto* old_base = bulk_block(old_bytes, alignment) ? bulk_base(pointer) : pointer;
            const auto old_offset =
                size_t(static_cast<unsigned char*>(pointer) - static_cast<unsigned char*>(old_base));
            const bool promoted = bulk_block(new_bytes, alignment);
            // On a downward threshold crossing realloc must retain the old payload
            // offset until memmove has moved the surviving bytes to the plain base.
            const auto extra = promoted ? sizeof(void*) + bulk_alignment - 1 : old_offset;
            if (new_bytes > SIZE_MAX - extra)
                throw std::bad_alloc{};
            auto* base = ::realloc(old_base, new_bytes + extra);
            if (!base)
                throw std::bad_alloc{};
            auto* output = promoted ? bulk_pointer(base) : static_cast<unsigned char*>(base);
            auto* input = static_cast<unsigned char*>(base) + old_offset;
            const auto copied = old_bytes < new_bytes ? old_bytes : new_bytes;
            if (output != input && copied)
                memmove(output, input, copied);
            if (promoted)
                memcpy(output - sizeof(void*), &base, sizeof(base));
            return output;
        }

        inline void* container_reallocate(void* pointer, size_t old_bytes, size_t new_bytes, size_t alignment)
        {
            if (!alignment || (alignment & (alignment - 1)))
                throw std::bad_alloc{};
            if (bulk_block(old_bytes, alignment) || bulk_block(new_bytes, alignment))
                return bulk_reallocate(pointer, old_bytes, new_bytes, alignment);
            return heap_reallocate(pointer, old_bytes, new_bytes, alignment);
        }
    } // namespace detail

    // Allocator copies/moves must be noexcept; the allocator travels with
    // ownership.
    struct heap_allocator
    {
        using byte_relocation_allocator = heap_allocator;

        // Byte relocation is used only for implicitly-lifetime, trivially copyable T.
        void* reallocate(void* p, size_t old_bytes, size_t new_bytes, size_t alignment)
        {
            return detail::container_reallocate(p, old_bytes, new_bytes, alignment);
        }

        void* allocate(size_t bytes, size_t alignment)
        {
            return detail::container_allocate(bytes, alignment);
        }

        void deallocate(void* p, size_t bytes, size_t alignment) noexcept
        {
            detail::container_deallocate(p, bytes, alignment);
        }

        bool operator==(const heap_allocator&) const noexcept
        {
            return true;
        }
    };

    class memory_resource
    {
      public:
        virtual ~memory_resource() = default;
        virtual void* allocate(size_t bytes, size_t alignment) = 0;
        virtual void deallocate(void* p, size_t bytes, size_t alignment) noexcept = 0;
    };

    class heap_resource final : public memory_resource
    {
      public:
        void* allocate(size_t n, size_t a) override
        {
            return detail::container_allocate(n, a);
        }

        void deallocate(void* p, size_t n, size_t a) noexcept override
        {
            detail::container_deallocate(p, n, a);
        }
    };

    inline memory_resource& default_resource()
    {
        static heap_resource r;
        return r;
    }

    struct resource_allocator
    {
        memory_resource* resource = nullptr;
        resource_allocator() noexcept = default;

        explicit resource_allocator(memory_resource& r) noexcept : resource(&r)
        {
        }

        memory_resource& get() const
        {
            return resource ? *resource : default_resource();
        }

        void* allocate(size_t n, size_t a)
        {
            return get().allocate(n, a);
        }

        void deallocate(void* p, size_t n, size_t a) noexcept
        {
            get().deallocate(p, n, a);
        }

        bool operator==(const resource_allocator& o) const noexcept
        {
            return &get() == &o.get();
        }
    };

    // Caller-owned memory; reset invalidates every allocation. No fallback
    // allocation.
    class fixed_buffer_resource final : public memory_resource
    {
        unsigned char* base_;
        size_t bytes_, used_ = 0;

      public:
        fixed_buffer_resource(void* p, size_t n) noexcept : base_(static_cast<unsigned char*>(p)), bytes_(n)
        {
        }

        void* allocate(size_t n, size_t a) override
        {
            if (!a || (a & (a - 1)))
                throw std::bad_alloc();
            if (!n)
                return nullptr;
            auto b = reinterpret_cast<uintptr_t>(base_);
            if (used_ > UINTPTR_MAX - b || a - 1 > UINTPTR_MAX - b - used_)
                throw std::bad_alloc();
            auto aligned = (b + used_ + a - 1) & ~uintptr_t(a - 1);
            auto offset = size_t(aligned - b);
            if (offset > bytes_ || n > bytes_ - offset)
                throw std::bad_alloc();
            used_ = offset + n;
            return reinterpret_cast<void*>(aligned);
        }

        void deallocate(void*, size_t, size_t) noexcept override
        {
        }

        void reset() noexcept
        {
            used_ = 0;
        }

        size_t used() const noexcept
        {
            return used_;
        }
    };

    // Phase-lifetime allocator. T destructors remain the containers'
    // responsibility.
    class monotonic_resource final : public memory_resource
    {
        struct block
        {
            block* next;
            size_t bytes, used;
        };

        block* head_ = nullptr;
        size_t chunk_;
        memory_resource* upstream_;

      public:
        explicit monotonic_resource(size_t chunk = 65536, memory_resource* upstream = nullptr)
            : chunk_(chunk), upstream_(upstream ? upstream : &default_resource())
        {
        }

        monotonic_resource(const monotonic_resource&) = delete;
        monotonic_resource& operator=(const monotonic_resource&) = delete;

        ~monotonic_resource()
        {
            release();
        }

        void* allocate(size_t n, size_t a) override
        {
            if (!n)
                return nullptr;
            if (!a || (a & (a - 1)) || n > SIZE_MAX - sizeof(block) || a - 1 > SIZE_MAX - sizeof(block) - n)
                throw std::bad_alloc();
            if (head_)
            {
                auto b = reinterpret_cast<uintptr_t>(head_);
                if (head_->used <= UINTPTR_MAX - b && a - 1 <= UINTPTR_MAX - b - head_->used)
                {
                    auto p = (b + head_->used + a - 1) & ~uintptr_t(a - 1);
                    auto off = size_t(p - b);
                    if (off <= head_->bytes && n <= head_->bytes - off)
                    {
                        head_->used = off + n;
                        return reinterpret_cast<void*>(p);
                    }
                }
            }
            auto need = sizeof(block) + a - 1 + n;
            auto bytes = chunk_ > need ? chunk_ : need;
            auto p = static_cast<block*>(upstream_->allocate(bytes, alignof(block)));
            ::new (static_cast<void*>(p)) block{head_, bytes, sizeof(block)};
            head_ = p;
            auto b = reinterpret_cast<uintptr_t>(p);
            auto address = (b + sizeof(block) + a - 1) & ~uintptr_t(a - 1);
            p->used = size_t(address - b) + n;
            return reinterpret_cast<void*>(address);
        }

        void deallocate(void*, size_t, size_t) noexcept override
        {
        }

        void release() noexcept
        {
            while (head_)
            {
                auto p = head_;
                head_ = p->next;
                upstream_->deallocate(p, p->bytes, alignof(block));
            }
        }
    };
} // namespace ce
