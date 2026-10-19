#pragma once
#include "../Utility_Framework/StandardLibrary.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace ce
{
    namespace capture_detail
    {
        struct heap_owner
        {
#if defined(_WIN32)
            HANDLE handle = HeapCreate(0, 1u << 20, 0);
            heap_owner()
            {
                if (!handle)
                {
                    throw std::bad_alloc();
                }
            }
            ~heap_owner() { HeapDestroy(handle); }
#endif
        };

        inline std::shared_ptr<heap_owner> heap()
        {
            // Allocators in retained snapshots keep the heap alive independently
            // of the collector and the static owner at shutdown.
            static auto owner = std::make_shared<heap_owner>();
            return owner;
        }
    } // namespace capture_detail

    template<class T>
    class capture_allocator
    {
      public:
        using value_type = T;
        capture_allocator() : heap_(capture_detail::heap()) {}
        capture_allocator(const capture_allocator&) noexcept = default;
        capture_allocator& operator=(const capture_allocator&) noexcept = default;
        // Standard containers may reuse their moved-from allocator. Keep its
        // heap owner valid instead of moving a shared_ptr out of it.
        capture_allocator(capture_allocator&& other) noexcept : heap_(other.heap_) {}
        capture_allocator& operator=(capture_allocator&& other) noexcept
        {
            heap_ = other.heap_;
            return *this;
        }
        template<class U>
        capture_allocator(const capture_allocator<U>& other) noexcept : heap_(other.heap_)
        {
        }
        T* allocate(std::size_t count)
        {
            if (count > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
#if defined(_WIN32)
            static_assert(alignof(T) <= MEMORY_ALLOCATION_ALIGNMENT);
            auto* memory = HeapAlloc(heap_->handle, 0, count * sizeof(T));
            if (!memory)
            {
                throw std::bad_alloc();
            }
            return static_cast<T*>(memory);
#else
            return std::allocator<T>{}.allocate(count);
#endif
        }
        void deallocate(T* memory, std::size_t count) noexcept
        {
#if defined(_WIN32)
            (void)count;
            HeapFree(heap_->handle, 0, memory);
#else
            std::allocator<T>{}.deallocate(memory, count);
#endif
        }
        template<class U>
        bool operator==(const capture_allocator<U>& other) const noexcept
        {
            return heap_ == other.heap_;
        }

      private:
        template<class>
        friend class capture_allocator;
        std::shared_ptr<capture_detail::heap_owner> heap_;
    };
} // namespace ce
