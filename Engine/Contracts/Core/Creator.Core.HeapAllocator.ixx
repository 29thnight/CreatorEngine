export module Creator.Core.HeapAllocator;

import std;
import "Creator.MemoryProvider.h";

export namespace ce
{
    struct heap_byte_allocator
    {
        void* allocate(std::size_t bytes,std::size_t alignment)
        {
            if(!alignment || (alignment&(alignment-1)))throw std::bad_alloc{};
            if(void* memory=mi_malloc_aligned(bytes,alignment))return memory;
            throw std::bad_alloc{};
        }
        void deallocate(void* memory,std::size_t,std::size_t)noexcept{mi_free(memory);}
        bool operator==(const heap_byte_allocator&)const noexcept{return true;}
    };
    template<typename T>
    class stl_heap_allocator
    {
      public:
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;

        stl_heap_allocator() noexcept = default;
        template<typename U>
        stl_heap_allocator(const stl_heap_allocator<U>&) noexcept
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
        bool operator==(const stl_heap_allocator<U>&) const noexcept
        {
            return true;
        }
    };

} // namespace ce

// Byte allocation callbacks preserve mimalloc's nullable, noexcept contract.
export namespace ce::heap
{
    inline void* try_allocate(std::size_t bytes) noexcept { return mi_malloc(bytes); }
    inline void* try_allocate_aligned(std::size_t bytes, std::size_t alignment) noexcept { return mi_malloc_aligned(bytes, alignment); }
    inline void deallocate(void* memory) noexcept { mi_free(memory); }
}
