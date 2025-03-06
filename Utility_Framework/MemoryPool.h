#pragma once
#include <cstddef>
#include <new>

template<typename type, size_t BlockSize>
concept PoolPolicy = (BlockSize >= 2 * sizeof(type));

template<typename type, size_t BlockSize = 4096> requires PoolPolicy<type, BlockSize>
class MemoryPool
{
public:
    template<typename U>
    using rebind = MemoryPool<U, BlockSize>;

    MemoryPool() noexcept;
    MemoryPool(const MemoryPool& memoryPool) noexcept;
    MemoryPool(MemoryPool&& memoryPool) noexcept;

    template<typename U>
    MemoryPool(const MemoryPool<U, BlockSize>& memoryPool) noexcept;

    ~MemoryPool() noexcept;

    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool& operator=(MemoryPool&& memoryPool) noexcept;

    type* address(type& x) const noexcept;
    const type* address(const type& x) const noexcept;

    type* allocate(size_t n = 1, const type* hint = 0);
    void deallocate(type* p, size_t n = 1) noexcept;

    size_t max_size() const noexcept;

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args);

    template<typename U>
    void destroy(U* p);

    template<typename... Args>
    type* allocate_element(Args&&... args);

    void deallocate_element(type* p);

private:
    union Node
    {
        type element;
        Node* next;
    };

    Node* currentBlock = nullptr;
    Node* currentSlot = nullptr;
    Node* lastSlot = nullptr;
    Node* freeSlots = nullptr;

    size_t alignment_padding(type* p, size_t align) const noexcept;
    void allocateBlock();

};

#include "MemoryPool.inl"
