#pragma once
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include "../../ThirdParty/mimalloc/include/mimalloc.h"

// Renderer-owned CPU storage only. Cross-thread destruction uses mi_free;
// no private heap is destroyed while a recording or GPU retirement owns data.
namespace ce
{
    template<typename T>
    class render_allocator
    {
      public:
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;

        render_allocator() noexcept = default;
        template<typename U>
        render_allocator(const render_allocator<U>&) noexcept
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
        bool operator==(const render_allocator<U>&) const noexcept
        {
            return true;
        }
    };

    template<typename T>
    using render_vector = std::vector<T, render_allocator<T>>;

    template<typename T>
    struct render_delete
    {
        void operator()(T* value) const noexcept
        {
            if (value)
            {
                std::destroy_at(value);
                render_allocator<T>{}.deallocate(value, 1);
            }
        }
    };

    template<typename T>
    using render_unique_ptr = std::unique_ptr<T, render_delete<T>>;

    template<typename T, typename... Args>
    render_unique_ptr<T> make_render_unique(Args&&... args)
    {
        auto* storage = render_allocator<T>{}.allocate(1);
        try
        {
            return render_unique_ptr<T>(std::construct_at(storage, std::forward<Args>(args)...));
        }
        catch (...)
        {
            render_allocator<T>{}.deallocate(storage, 1);
            throw;
        }
    }

    template<typename T, typename... Args>
    std::shared_ptr<T> make_render_shared(Args&&... args)
    {
        return std::allocate_shared<T>(render_allocator<T>{}, std::forward<Args>(args)...);
    }
} // namespace ce
