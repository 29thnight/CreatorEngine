#pragma once
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include "../../ThirdParty/mimalloc/include/mimalloc.h"

// General CPU heap storage. Stateless allocators share the same mimalloc
// provider, so an allocation may be released by another thread.
namespace ce
{
    template<typename T>
    class heap_allocator
    {
      public:
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;

        heap_allocator() noexcept = default;
        template<typename U>
        heap_allocator(const heap_allocator<U>&) noexcept
        {
        }

        [[nodiscard]] T* allocate(size_t count)
        {
            if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            auto* memory = mi_malloc_aligned(count * sizeof(T), alignof(T));
            if (!memory)
            {
                throw std::bad_alloc();
            }
            return static_cast<T*>(memory);
        }

        void deallocate(T* memory, size_t) noexcept { mi_free(memory); }

        template<typename U>
        bool operator==(const heap_allocator<U>&) const noexcept
        {
            return true;
        }
    };

} // namespace ce
