#pragma once
#include "MemoryPool.h"

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline MemoryPool<type, BlockSize>::MemoryPool() noexcept :
    currentBlock(nullptr), currentSlot(nullptr), lastSlot(nullptr), freeSlots(nullptr)
{
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline MemoryPool<type, BlockSize>::MemoryPool(const MemoryPool& memoryPool) noexcept :
    MemoryPool()
{
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline MemoryPool<type, BlockSize>::MemoryPool(MemoryPool&& memoryPool) noexcept :
    MemoryPool()
{
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize> template<typename U>
inline MemoryPool<type, BlockSize>::MemoryPool(const MemoryPool<U, BlockSize>& memoryPool) noexcept :
    MemoryPool()
{
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
template<typename U, typename ...Args>
inline void MemoryPool<type, BlockSize>::construct(U* p, Args && ...args)
{
    new(p) U(std::forward<Args>(args)...);
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
template<typename U>
inline void MemoryPool<type, BlockSize>::destroy(U* p)
{
    p->~U();
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
template<typename ...Args>
inline type* MemoryPool<type, BlockSize>::allocate_element(Args && ...args)
{
    type* result = allocate();
    construct(result, std::forward<Args>(args)...);
    return result;
}


template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline MemoryPool<type, BlockSize>::~MemoryPool() noexcept
{
    Node* curr = currentBlock;
    while (curr != nullptr)
    {
        Node* prev = curr->next;
        std::free(reinterpret_cast<void*>(curr));
        curr = prev;
    }
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline MemoryPool<type, BlockSize>& MemoryPool<type, BlockSize>::operator=(MemoryPool&& memoryPool) noexcept
{
    if (this != &memoryPool)
    {
        std::swap(currentBlock, memoryPool.currentBlock);
        std::swap(currentSlot, memoryPool.currentSlot);
        std::swap(lastSlot, memoryPool.lastSlot);
        std::swap(freeSlots, memoryPool.freeSlots);
    }
    return *this;
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline type* MemoryPool<type, BlockSize>::address(type& x) const noexcept
{
    return &x;
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline const type* MemoryPool<type, BlockSize>::address(const type& x) const noexcept
{
    return &x;
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline type* MemoryPool<type, BlockSize>::allocate(size_t n, const type* hint)
{
    if (nullptr != freeSlots)
    {
        type* result = reinterpret_cast<type*>(freeSlots);
        freeSlots = freeSlots->next;
        return result;
    }
    else
    {
        if (currentSlot >= lastSlot)
        {
            allocateBlock();
        }
        return reinterpret_cast<type*>(currentSlot++);
    }
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline void MemoryPool<type, BlockSize>::deallocate(type* p, size_t n) noexcept
{
    if (nullptr != p)
    {
        Node* slot = reinterpret_cast<Node*>(p);
        slot->next = freeSlots;
        freeSlots = slot;
    }
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline size_t MemoryPool<type, BlockSize>::max_size() const noexcept
{
    size_t maxBlocks = -1 / BlockSize;
    return (BlockSize - sizeof(Node)) / sizeof(type) * maxBlocks;
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline void MemoryPool<type, BlockSize>::deallocate_element(type* p)
{
    if (nullptr != p)
    {
        p->~type();
        deallocate(p);
    }
}

template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline size_t MemoryPool<type, BlockSize>::alignment_padding(type* p, size_t align) const noexcept
{
    uintptr_t result = reinterpret_cast<uintptr_t>(p);
    return ((align - result) % align);
}



template<typename type, size_t BlockSize> requires PoolPolicy<type, BlockSize>
inline void MemoryPool<type, BlockSize>::allocateBlock()
{
    type* newBlock = reinterpret_cast<type*>(std::malloc(BlockSize));
    reinterpret_cast<Node*>(newBlock)->next = currentBlock;
    currentBlock = reinterpret_cast<Node*>(newBlock);

    type* body = newBlock + sizeof(Node*);
    size_t bodyPadding = alignment_padding(body, alignof(Node));

    currentSlot = reinterpret_cast<Node*>(body + bodyPadding);
    lastSlot = reinterpret_cast<Node*>(newBlock + BlockSize - sizeof(Node) + 1);
}
