#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.HeapAllocator.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../../Engine/Contracts/External/Creator.MemoryProvider.h"

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

        [[nodiscard]] T* allocate(std::size_t count)
        {
            if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
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

        void deallocate(T* memory, std::size_t) noexcept { mi_free(memory); }

        template<typename U>
        bool operator==(const heap_allocator<U>&) const noexcept
        {
            return true;
        }
    };

} // namespace ce

// Byte allocation callbacks preserve mimalloc's nullable, noexcept contract.
namespace ce::heap
{
    inline void* try_allocate(std::size_t bytes) noexcept { return mi_malloc(bytes); }
    inline void* try_allocate_aligned(std::size_t bytes, std::size_t alignment) noexcept { return mi_malloc_aligned(bytes, alignment); }
    inline void deallocate(void* memory) noexcept { mi_free(memory); }
}
