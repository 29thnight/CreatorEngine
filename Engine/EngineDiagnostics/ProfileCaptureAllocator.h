#pragma once
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
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
        friend class capture_byte_allocator;
        template<class>
        friend class capture_allocator;
        std::shared_ptr<capture_detail::heap_owner> heap_;
    };

    // Byte policy for internal containers. Retained snapshots keep the same
    // private heap alive; the STL allocator above remains at shared_ptr control
    // block boundaries until ownership migration reaches that boundary.
    class capture_byte_allocator
    {
      public:
        capture_byte_allocator() : heap_(capture_detail::heap()) {}
        capture_byte_allocator(const capture_byte_allocator&) noexcept = default;
        capture_byte_allocator& operator=(const capture_byte_allocator&) noexcept = default;
        capture_byte_allocator(capture_byte_allocator&& other) noexcept : heap_(other.heap_) {}
        capture_byte_allocator& operator=(capture_byte_allocator&& other) noexcept { heap_=other.heap_;return *this; }
        template<class T> capture_byte_allocator(const capture_allocator<T>& other) noexcept : heap_(other.heap_) {}
        void* allocate(size_t bytes,size_t alignment)
        {
            if(!alignment || (alignment&(alignment-1)))throw std::bad_alloc{};
#if defined(_WIN32)
            const auto extra=alignment>MEMORY_ALLOCATION_ALIGNMENT?alignment-1+sizeof(void*):size_t{};
            if(bytes>SIZE_MAX-extra)throw std::bad_alloc{};
            auto* base=HeapAlloc(heap_->handle,0,bytes+extra);
            if(!base)throw std::bad_alloc{};
            if(!extra)return base;
            const auto address=(reinterpret_cast<uintptr_t>(base)+sizeof(void*)+alignment-1)&~uintptr_t(alignment-1);
            auto* output=reinterpret_cast<unsigned char*>(address);
            std::memcpy(output-sizeof(void*),&base,sizeof(base));return output;
#else
            return ce::heap_allocator{}.allocate(bytes,alignment);
#endif
        }
        void deallocate(void* pointer,size_t bytes,size_t alignment)noexcept
        {
            if(!pointer)return;
#if defined(_WIN32)
            (void)bytes;
            void* base=pointer;
            if(alignment>MEMORY_ALLOCATION_ALIGNMENT)
                std::memcpy(&base,static_cast<unsigned char*>(pointer)-sizeof(void*),sizeof(base));
            HeapFree(heap_->handle,0,base);
#else
            ce::heap_allocator{}.deallocate(pointer,bytes,alignment);
#endif
        }
        bool operator==(const capture_byte_allocator& other)const noexcept { return heap_==other.heap_; }
      private:
        std::shared_ptr<capture_detail::heap_owner> heap_;
    };
} // namespace ce
