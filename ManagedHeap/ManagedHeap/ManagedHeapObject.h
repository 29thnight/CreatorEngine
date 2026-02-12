#pragma once
#include <memory>
#include <type_traits>
#include <utility>
#include <concepts>
#include <string>
#include <vector>
#include <list>
#include <deque>
#include <forward_list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <stack>
#include <functional>
#include "MemoryManager.h"
//#define GC_NO_INLINE_STD_NEW
//#include "gc_cpp.h"

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

    class GCObject {}; // GCObject는 HeapObject와 동일

    struct NoDelete {
        void operator()(GCObject* p) const noexcept {
            // 소멸자도 안 부름, delete도 안 함
        }
    };

	template<typename T> requires std::is_base_of_v<GCObject, T>
    using GcHandle = std::shared_ptr<T>;   // + NoDelete 붙여서 생성
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
	bool operator==(const MyAllocator& other) const noexcept { return true; }
	bool operator!=(const MyAllocator& other) const noexcept { return false; }
};

template<typename T, typename U>
bool operator==(const MyAllocator<T>&, const MyAllocator<U>&) { return true; }

template<typename T, typename U>
bool operator!=(const MyAllocator<T>&, const MyAllocator<U>&) { return false; }

template<typename T>
concept IsManagedObject = std::is_base_of_v<Managed::HeapObject, T>;

//template<typename T, typename... Args>
//Managed::GcHandle<T> gc_alloc(Args&&... args)
//{
//    static_assert(std::is_base_of_v<Managed::GCObject, T>, "T must be a Managed::GCObject");
//    T* ptr = new T(std::forward<Args>(args)...);
//    return Managed::GcHandle<T>(ptr, Managed::NoDelete());
//}

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

namespace ce
{
    template<typename T>
    using Alloc = MyAllocator<T>;

    // -----------------------------
    // 1) 기본 시퀀스 컨테이너
    // -----------------------------
    template<typename T>
    using vector = std::vector<T, Alloc<T>>;

    template<typename T>
    using list = std::list<T, Alloc<T>>;

    template<typename T>
    using deque = std::deque<T, Alloc<T>>;

    template<typename T>
    using forward_list = std::forward_list<T, Alloc<T>>;

    // -----------------------------
    // 2) 문자열 계열
    // -----------------------------
    template<typename CharT>
    using basic_string =
        std::basic_string<CharT, std::char_traits<CharT>, Alloc<CharT>>;

    using string = basic_string<char>;
    using wstring = basic_string<wchar_t>;

    // -----------------------------
    // 3) 연관 컨테이너 (정렬)
    // -----------------------------
    template<
        typename Key,
        typename T,
        typename Compare = std::less<Key>
    >
    using map = std::map<
        Key,
        T,
        Compare,
        Alloc<std::pair<const Key, T>>
    >;

    template<
        typename Key,
        typename T,
        typename Compare = std::less<Key>
    >
    using multimap = std::multimap<
        Key,
        T,
        Compare,
        Alloc<std::pair<const Key, T>>
    >;

    template<
        typename Key,
        typename Compare = std::less<Key>
    >
    using set = std::set<
        Key,
        Compare,
        Alloc<Key>
    >;

    template<
        typename Key,
        typename Compare = std::less<Key>
    >
    using multiset = std::multiset<
        Key,
        Compare,
        Alloc<Key>
    >;

    // -----------------------------
    // 4) 연관 컨테이너 (unordered)
    // -----------------------------
    template<
        typename Key,
        typename T,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>
    >
    using unordered_map = std::unordered_map<
        Key,
        T,
        Hash,
        KeyEqual,
        Alloc<std::pair<const Key, T>>
    >;

    template<
        typename Key,
        typename T,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>
    >
    using unordered_multimap = std::unordered_multimap<
        Key,
        T,
        Hash,
        KeyEqual,
        Alloc<std::pair<const Key, T>>
    >;

    template<
        typename Key,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>
    >
    using unordered_set = std::unordered_set<
        Key,
        Hash,
        KeyEqual,
        Alloc<Key>
    >;

    template<
        typename Key,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>
    >
    using unordered_multiset = std::unordered_multiset<
        Key,
        Hash,
        KeyEqual,
        Alloc<Key>
    >;

    // -----------------------------
    // 5) 어댑터 컨테이너 (queue / stack / priority_queue)
    //  - 내부 컨테이너에 위에서 정의한 ce::deque / ce::vector 사용
    // -----------------------------
    template<typename T>
    using queue = std::queue<T, ce::deque<T>>;

    template<typename T>
    using stack = std::stack<T, ce::deque<T>>;

    template<
        typename T,
        typename Container = ce::vector<T>,
        typename Compare = std::less<typename Container::value_type>
    >
    using priority_queue = std::priority_queue<T, Container, Compare>;
}