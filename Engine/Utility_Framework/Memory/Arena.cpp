#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Arena.h"
#include "../../Contracts/External/Creator.MemoryProvider.h"
#else
module Creator.Core.Arena;
import "../../Contracts/External/Creator.MemoryProvider.h";
#endif

namespace ce
{
    arena_resource::~arena_resource()
    {
        while (head_)
        {
            auto* next = head_->next;
            mi_free(head_);
            head_ = next;
        }
    }

    void arena_resource::reset() noexcept
    {
        for (auto* entry = head_; entry; entry = entry->next)
        {
            entry->used = 0;
        }
        current_ = head_;
        stats_.active_blocks = 0;
        stats_.active_capacity = 0;
        stats_.used_bytes = 0;
        ++stats_.resets;
    }

    void* arena_resource::allocate(std::size_t bytes, std::size_t alignment)
    {
        if (!alignment || (alignment & (alignment - 1)) || bytes > SIZE_MAX - alignment - sizeof(block))
        {
            throw std::bad_alloc();
        }
        bytes = (std::max)(bytes, std::size_t{1});
        void* memory{};
        if (try_allocate(bytes, alignment, memory))
        {
            return memory;
        }
        auto* entry = current_;
        block* previous{};
        while (entry)
        {
            auto* begin = entry->data() + entry->used;
            std::size_t available = entry->capacity - entry->used;
            void* aligned = begin;
            if (std::align(alignment, bytes, aligned, available))
            {
                if (!entry->used)
                {
                    ++stats_.active_blocks;
                    stats_.active_capacity += entry->capacity;
                }
                entry->used = static_cast<std::byte*>(aligned) - entry->data() + bytes;
                stats_.used_bytes += bytes;
                current_ = entry;
                return aligned;
            }
            previous = entry;
            entry = entry->next;
        }
        const std::size_t capacity = (std::max)(std::size_t{65536}, bytes + alignment - 1);
        memory = mi_malloc(sizeof(block) + capacity);
        if (!memory)
        {
            throw std::bad_alloc();
        }
        entry = new (memory) block{nullptr, capacity, 0};
        if (previous)
        {
            previous->next = entry;
        }
        else if (!head_)
        {
            head_ = entry;
        }
        else
        {
            // current_ is null only for a fresh resource.
            mi_free(entry);
            throw std::logic_error("Arena lost its retained block cursor.");
        }
        current_ = entry;
        ++stats_.block_count;
        stats_.retained_bytes += capacity;
        return allocate(bytes, alignment);
    }

} // namespace ce
