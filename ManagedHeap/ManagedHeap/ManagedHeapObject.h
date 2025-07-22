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

template<typename T>
concept IsManagedObject = std::is_base_of_v<ManagedHeapObject, T>;

template<typename T, typename... Args>
std::shared_ptr<T> shared_alloc(Args&&... args)
{
    static_assert(IsManagedObject<T>, "T must be a ManagedHeapObject");
    
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), [](T* ptr) {
        ptr->~T();
        MyFree(ptr);
    });
}