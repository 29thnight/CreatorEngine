#include "block_allocator.hpp"
#include "block_allocator_testing.hpp"

#include "gc/config.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <new>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#    define GCCE_ASAN 1
#elif defined(__has_feature)
#    if __has_feature(address_sanitizer)
#        define GCCE_ASAN 1
#    endif
#endif

#if defined(GCCE_ASAN)
#    include <sanitizer/asan_interface.h>
#    define GCCE_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#    define GCCE_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#else
#    define GCCE_POISON(p, n) ((void)(p), (void)(n))
#    define GCCE_UNPOISON(p, n) ((void)(p), (void)(n))
#endif

namespace gc::detail
{
namespace
{
thread_local allocator_test_state* test_state = nullptr;

void fail_if_requested(allocator_failure point)
{
    if (test_state && test_state->fail_at == point)
    {
        test_state->fail_at = allocator_failure::none;
        throw std::bad_alloc();
    }
}

constexpr std::array<std::size_t, 24> class_sizes{16,  32,  48,  64,  80,   96,   112,  128,
                                                  160, 192, 224, 256, 320,  384,  448,  512,
                                                  640, 768, 896, 1024, 1280, 1536, 1792, 2048};

// Size class of each 16-byte step up to 2048, for O(1) lookup.
constexpr auto class_lookup = [] {
    std::array<std::uint8_t, 2048 / 16 + 1> t{};
    std::size_t c = 0;
    for (std::size_t step = 0; step < t.size(); ++step)
    {
        while (class_sizes[c] < step * 16)
            ++c;
        t[step] = static_cast<std::uint8_t>(c);
    }
    return t;
}();

// Blocks start after the page header and stay 16-byte aligned.
constexpr std::size_t page_header_size = 64;
constexpr std::uint32_t all_pages_mask = 0xFFFFFFFFu;

#if !defined(NDEBUG) && !defined(GCCE_ASAN)
constexpr unsigned char freed_fill = 0xDD;
#endif

// ---- OS virtual memory. Chunks are aligned to page_size so that a block's
// page header is found by masking its address.

// Reserves address space. Pages are committed one by one by os_recommit
// (Windows) or on first touch (POSIX).
void* os_map(std::size_t size, std::size_t align)
{
#if defined(_WIN32)
    // VirtualAlloc returns allocation-granularity (64 KiB) aligned memory.
    (void)align;
    void* p = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
    if (!p)
        throw std::bad_alloc();
    return p;
#else
    void* raw = ::mmap(nullptr, size + align, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED)
        throw std::bad_alloc();
    const auto start = reinterpret_cast<std::uintptr_t>(raw);
    const auto aligned = (start + align - 1) & ~(std::uintptr_t{align} - 1);
    if (aligned > start)
        ::munmap(raw, aligned - start);
    const std::size_t tail = (start + size + align) - (aligned + size);
    if (tail)
        ::munmap(reinterpret_cast<void*>(aligned + size), tail);
    return reinterpret_cast<void*>(aligned);
#endif
}

void os_unmap(void* p, std::size_t size) noexcept
{
#if defined(_WIN32)
    (void)size;
    ::VirtualFree(p, 0, MEM_RELEASE);
#else
    ::munmap(p, size);
#endif
    if (test_state)
        ++test_state->chunk_unmaps;
}

// Returns the physical memory of a range while keeping its addresses.
void os_decommit(void* p, std::size_t size) noexcept
{
#if defined(_WIN32)
    ::VirtualFree(p, size, MEM_DECOMMIT);
#else
    ::madvise(p, size, MADV_DONTNEED);
#endif
}

void os_commit(void* p, std::size_t size)
{
#if defined(_WIN32)
    if (!::VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE))
        throw std::bad_alloc();
#else
    (void)p; // anonymous mappings fault zero pages back in on access
    (void)size;
#endif
}
} // namespace

allocator_test_state* set_allocator_test_state(allocator_test_state* state) noexcept
{
    allocator_test_state* previous = test_state;
    test_state = state;
    return previous;
}

struct block_allocator::chunk
{
    unsigned char* base = nullptr;
    std::uint32_t free_mask = all_pages_mask;      // bit i: page i is not in use
    std::uint32_t committed_mask = 0;              // bit i: page i holds physical memory
    std::array<std::uint32_t, 32> idle_since{};    // cycle in which page i became idle
    std::size_t index = 0;                         // position in chunks_
    chunk* prev = nullptr;                         // in chunks_with_free_
    chunk* next = nullptr;
    bool listed = false;
};

struct block_allocator::page
{
    page* prev = nullptr; // in its pool's available list
    page* next = nullptr;
    void* free_list = nullptr;
    unsigned char* bump = nullptr; // never-used blocks start here
    unsigned char* end = nullptr;
    chunk* owner = nullptr;
    std::uint32_t live = 0;
    std::uint16_t pool = 0;
    std::uint8_t index = 0; // page number within its chunk
    bool listed = false;
};

block_allocator::block_allocator(pool_mode mode) : mode_(mode)
{
    static_assert(sizeof(page) <= page_header_size);
    static_assert(pages_per_chunk == 32, "chunk masks are 32 bits");
    if (mode_ == pool_mode::size_classes)
    {
        pools_.reserve(class_sizes.size());
        for (std::size_t s : class_sizes)
            pools_.push_back({s, nullptr});
    }
}

block_allocator::~block_allocator()
{
    for (chunk* c : chunks_)
    {
        if (!leaked_)
        {
            GCCE_UNPOISON(c->base, chunk_size);
            os_unmap(c->base, chunk_size);
        }
        delete c;
    }
}

void block_allocator::leak() noexcept { leaked_ = true; }

std::size_t block_allocator::committed_bytes() const noexcept
{
    return committed_pages_ * page_size + individual_bytes_;
}

void block_allocator::unlink(page*& head, page* p) noexcept
{
    if (p->prev)
        p->prev->next = p->next;
    else
        head = p->next;
    if (p->next)
        p->next->prev = p->prev;
    p->prev = p->next = nullptr;
    p->listed = false;
}

void block_allocator::push(page*& head, page* p) noexcept
{
    p->prev = nullptr;
    p->next = head;
    if (head)
        head->prev = p;
    head = p;
    p->listed = true;
}

void block_allocator::unlink_chunk(chunk* c) noexcept
{
    if (c->prev)
        c->prev->next = c->next;
    else
        chunks_with_free_ = c->next;
    if (c->next)
        c->next->prev = c->prev;
    c->prev = c->next = nullptr;
    c->listed = false;
}

void block_allocator::push_chunk(chunk* c) noexcept
{
    c->prev = nullptr;
    c->next = chunks_with_free_;
    if (chunks_with_free_)
        chunks_with_free_->prev = c;
    chunks_with_free_ = c;
    c->listed = true;
}

block_allocator::chunk* block_allocator::map_chunk()
{
    fail_if_requested(allocator_failure::chunk_metadata);
    auto* c = new chunk;
    c->index = chunks_.size();
    try
    {
        fail_if_requested(allocator_failure::chunk_mapping);
        c->base = static_cast<unsigned char*>(os_map(chunk_size, page_size));
        if (test_state)
            ++test_state->chunk_maps;
        fail_if_requested(allocator_failure::chunk_vector);
        chunks_.push_back(c);
        fail_if_requested(allocator_failure::chunk_index);
        chunk_index_.emplace(reinterpret_cast<std::uintptr_t>(c->base), c);
    }
    catch (...)
    {
        // Pointer-vector growth and map insertion leave their containers
        // unchanged on failure. Undo a completed vector append as well as
        // the mapping; nothing is on the free-chunk list yet.
        if (chunks_.size() != c->index)
            chunks_.pop_back();
        if (c->base)
            os_unmap(c->base, chunk_size);
        delete c;
        throw;
    }
    // All throwing work is finished before publishing reusable pages.
    GCCE_UNPOISON(c->base, chunk_size); // no stale shadow from an earlier mapping
    GCCE_POISON(c->base, chunk_size);
    push_chunk(c);
    return c;
}

void block_allocator::unmap_chunk(chunk* c) noexcept
{
    if (c->listed)
        unlink_chunk(c);
    const auto committed = static_cast<std::size_t>(std::popcount(c->committed_mask));
    committed_pages_ -= committed;
    idle_committed_pages_ -= committed;

    chunks_[c->index] = chunks_.back();
    chunks_[c->index]->index = c->index;
    chunks_.pop_back();
    chunk_index_.erase(reinterpret_cast<std::uintptr_t>(c->base));

    GCCE_UNPOISON(c->base, chunk_size);
    os_unmap(c->base, chunk_size);
    delete c;
}

block_allocator::page* block_allocator::new_page(std::uint16_t pool)
{
    // Prefer an idle page that still holds physical memory.
    chunk* c = nullptr;
    for (chunk* k = chunks_with_free_; k; k = k->next)
    {
        if (k->free_mask & k->committed_mask)
        {
            c = k;
            break;
        }
    }
    bool mapped = false;
    if (!c)
    {
        c = chunks_with_free_;
        if (!c)
        {
            c = map_chunk();
            mapped = true;
        }
    }

    const std::uint32_t warm = c->free_mask & c->committed_mask;
    const auto index = static_cast<unsigned>(std::countr_zero(warm ? warm : c->free_mask));
    const std::uint32_t bit = 1u << index;
    unsigned char* memory = c->base + std::size_t{index} * page_size;

    if (c->committed_mask & bit)
    {
        --idle_committed_pages_;
    }
    else
    {
        try
        {
            fail_if_requested(allocator_failure::page_commit);
            os_commit(memory, page_size);
        }
        catch (...)
        {
            // A failed first commit must not retain the mapping or either
            // index. Existing chunks and their free-page masks stay intact.
            if (mapped)
                unmap_chunk(c);
            throw;
        }
        c->committed_mask |= bit;
        ++committed_pages_;
    }
    c->free_mask &= ~bit;
    if (!c->free_mask)
        unlink_chunk(c);

    GCCE_UNPOISON(memory, page_header_size);
    auto* p = ::new (memory) page{};
    p->owner = c;
    p->index = static_cast<std::uint8_t>(index);
    p->pool = pool;
    p->bump = memory + page_header_size;
    const std::size_t block = pools_[pool].block_size;
    const std::size_t capacity = (page_size - page_header_size) / block;
    p->end = p->bump + capacity * block;
    ++pages_in_use_;
    push(pools_[pool].available, p);
    return p;
}

void block_allocator::release_page(page* p) noexcept
{
    if (p->listed)
        unlink(pools_[p->pool].available, p);
    chunk* c = p->owner;
    const std::uint32_t bit = 1u << p->index;
    unsigned char* memory = reinterpret_cast<unsigned char*>(p);
    --pages_in_use_;

    c->free_mask |= bit;
    c->idle_since[p->index] = cycle_;
    ++idle_committed_pages_;
    if (!c->listed)
        push_chunk(c);
    GCCE_POISON(memory, page_size);
}

void block_allocator::end_cycle() noexcept
{
    ++cycle_;
    // Backwards: unmap_chunk swap-removes from chunks_.
    for (std::size_t i = chunks_.size(); i-- > 0;)
    {
        chunk* c = chunks_[i];
        for (std::uint32_t idle = c->free_mask & c->committed_mask; idle;)
        {
            if (idle_committed_pages_ <= min_idle_committed_pages)
                break;
            const auto index = static_cast<unsigned>(std::countr_zero(idle));
            const std::uint32_t bit = 1u << index;
            idle &= ~bit;
            if (cycle_ - c->idle_since[index] < idle_cycles_before_decommit)
                continue;
            os_decommit(c->base + std::size_t{index} * page_size, page_size);
            c->committed_mask &= ~bit;
            --committed_pages_;
            --idle_committed_pages_;
        }
        // A wholly idle, decommitted chunk is unmapped, except the last chunk
        // with free pages.
        if (c->free_mask == all_pages_mask && c->committed_mask == 0 && (c->prev || c->next))
            unmap_chunk(c);
    }
}

block_allocator::allocation block_allocator::allocate(std::size_t size, std::size_t align)
{
    if (mode_ == pool_mode::system || align > small_align || size > max_pooled_size)
    {
        const bool plain = mode_ == pool_mode::system && align <= __STDCPP_DEFAULT_NEW_ALIGNMENT__;
        void* block = plain ? ::operator new(size) : ::operator new(size, std::align_val_t{std::max(align, small_align)});
#if GC_DEBUG_CHECKS
        try
        {
            fail_if_requested(allocator_failure::individual_index);
            individual_blocks_.emplace(reinterpret_cast<std::uintptr_t>(block), size);
        }
        catch (...)
        {
            plain ? ::operator delete(block) : ::operator delete(block, std::align_val_t{std::max(align, small_align)});
            throw;
        }
#endif
        individual_bytes_ += size;
        return {block, plain ? system_class : large_class};
    }

    const std::uint16_t pool = class_lookup[(size + small_align - 1) / small_align];
    pool_state& state = pools_[pool];
    page* p = state.available ? state.available : new_page(pool);

    void* block;
    if (p->free_list)
    {
        block = p->free_list;
        GCCE_UNPOISON(block, state.block_size);
        std::memcpy(&p->free_list, block, sizeof(void*));
    }
    else
    {
        block = p->bump;
        p->bump += state.block_size;
        GCCE_UNPOISON(block, state.block_size);
    }
    ++p->live;
    if (!p->free_list && p->bump == p->end)
        unlink(state.available, p); // full
    return {block, pool};
}

void block_allocator::deallocate(void* block, std::size_t size, std::size_t align, std::uint16_t pool) noexcept
{
    if (pool == system_class || pool == large_class)
    {
        individual_bytes_ -= size;
#if GC_DEBUG_CHECKS
        individual_blocks_.erase(reinterpret_cast<std::uintptr_t>(block));
#endif
        if (pool == system_class)
            ::operator delete(block);
        else
            ::operator delete(block, std::align_val_t{std::max(align, small_align)});
        return;
    }

    auto* p = reinterpret_cast<page*>(reinterpret_cast<std::uintptr_t>(block) & ~(page_size - 1));
    pool_state& state = pools_[pool];

#if !defined(NDEBUG) && !defined(GCCE_ASAN)
    std::memset(block, freed_fill, state.block_size);
#endif
    std::memcpy(block, &p->free_list, sizeof(void*));
    p->free_list = block;
    GCCE_POISON(block, state.block_size);
    --p->live;

    if (p->live == 0)
    {
        release_page(p); // the page can now serve any pool
        return;
    }
    if (!p->listed)
        push(state.available, p); // was full
}
void* block_allocator::find_block(const void* p) const noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    if (auto it = chunk_index_.upper_bound(a); it != chunk_index_.begin())
    {
        --it;
        const chunk* c = it->second;
        if (a < it->first + chunk_size)
        {
            const auto index = static_cast<unsigned>((a - it->first) / page_size);
            if (c->free_mask & (1u << index))
                return nullptr; // page not in use
            const auto* pg = reinterpret_cast<const page*>(it->first + std::size_t{index} * page_size);
            const auto first = reinterpret_cast<std::uintptr_t>(pg) + page_header_size;
            if (a < first || a >= reinterpret_cast<std::uintptr_t>(pg->bump))
                return nullptr; // header or never-used tail
            const std::size_t block = pools_[pg->pool].block_size;
            return reinterpret_cast<void*>(first + (a - first) / block * block);
        }
    }
    if (auto it = individual_blocks_.upper_bound(a); it != individual_blocks_.begin())
    {
        --it;
        if (a < it->first + it->second)
            return reinterpret_cast<void*>(it->first);
    }
    return nullptr;
}

} // namespace gc::detail
