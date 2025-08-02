#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <concepts>
#include "MemoryManager.h"

// Base class for all objects that need to be passed across EXE/DLL boundaries.
class ManagedHeapObject {
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
    virtual ~ManagedHeapObject() = default;
};

// 1. Custom allocator that uses MyAlloc/MyFree
template<typename T>
struct MyAllocator {
    using value_type = T;

    MyAllocator() = default;

    template<typename U>
    constexpr MyAllocator(const MyAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(MyAlloc(n * sizeof(T)));
    }

    void deallocate(T* p, std::size_t) noexcept {
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
concept IsManagedObject = std::is_base_of_v<ManagedHeapObject, T>;

template<typename T>
using ManagedUniquePtr = std::unique_ptr<T, void(*)(T*)>;

// 2. shared_alloc using allocate_shared
template<typename T, typename... Args>
std::shared_ptr<T> shared_alloc(Args&&... args)
{
    static_assert(IsManagedObject<T>, "T must be a ManagedHeapObject");

    return std::allocate_shared<T>(MyAllocator<T>(), std::forward<Args>(args)...);
}

// 3. unique_alloc using custom deleter
template<typename T, typename... Args>
ManagedUniquePtr<T> unique_alloc(Args&&... args)
{
    static_assert(IsManagedObject<T>, "T must be a ManagedHeapObject");

    T* ptr = new T(std::forward<Args>(args)...);
    return ManagedUniquePtr<T>(ptr, [](T* p) { MyFree(p); });
}