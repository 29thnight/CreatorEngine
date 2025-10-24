#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <concepts>
#include <string>
#include "MemoryManager.h"

namespace Managed
{
    //할당 영역을 명확하게 하기 위한 using 선언
    template<typename T>
    using UniquePtr = std::unique_ptr<T>;

    template<typename T>
    using SharedPtr = std::shared_ptr<T>;

    template<typename T>
    using WeakPtr = std::weak_ptr<T>;

    // Base class for all objects that need to be passed across EXE/DLL boundaries.
    class HeapObject {
    public:
        // Overload the new operator to use the custom allocation function.
        void* operator new(size_t size) {
            return MyAlloc(size);
        }
    
        // Overload the delete operator to use the custom free function.
        void operator delete(void* ptr) {
            MyFree(ptr);
        }
    
        // Overload array new and delete as well if needed.
        void* operator new[](size_t size) {
            return MyAlloc(size);
        }
    
        void operator delete[](void* ptr) {
            MyFree(ptr);
        }
    
        // Virtual destructor to ensure proper cleanup of derived classes.
        virtual ~HeapObject() = default;
    };
}

// 1. Custom allocator that uses MyAlloc/MyFree
template<typename T>
struct MyAllocator {
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::true_type; // 상태가 없다면 true

    MyAllocator() = default;

    template<typename U>
    constexpr MyAllocator(const MyAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) 
    {
        return static_cast<T*>(MyAlloc(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) noexcept 
    {
        MyFree(p);
    }

    // For compatibility
    template<typename U> struct rebind { using other = MyAllocator<U>; };
};

template<typename T, typename U>
bool operator==(const MyAllocator<T>&, const MyAllocator<U>&) { return true; }

template<typename T, typename U>
bool operator!=(const MyAllocator<T>&, const MyAllocator<U>&) { return false; }

template<typename T>
concept IsManagedObject = std::is_base_of_v<Managed::HeapObject, T>;

// 2. shared_alloc using allocate_shared
template<typename T, typename... Args>
Managed::SharedPtr<T> shared_alloc(Args&&... args)
{
    static_assert(IsManagedObject<T>, "T must be a Managed::HeapObject");

    return std::allocate_shared<T>(MyAllocator<T>(), std::forward<Args>(args)...);
}

// 3. unique_alloc using custom deleter
template<typename T, typename... Args>
Managed::UniquePtr<T> unique_alloc(Args&&... args)
{
    static_assert(IsManagedObject<T>, "T must be a Managed::HeapObject");

    T* ptr = new T(std::forward<Args>(args)...);
    return Managed::UniquePtr<T>(ptr);
}