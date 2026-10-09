#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace gc::detail
{
// Storage for GC blocks (object header + object). Owned by one domain and
// used from its owner thread only.
//
// Small blocks come from 64 KiB pages split into size classes; a freed block
// goes back to its page's free list. Pages come from 2 MiB chunks of OS
// virtual memory (mmap / VirtualAlloc), so freed pages give their physical
// memory back to the OS instead of fragmenting the C++ heap:
//
//   - an emptied page returns to its chunk and can serve any pool;
//   - idle pages stay committed so that the next burst of allocation reuses
//     them without page faults; end_cycle() decommits pages that stayed idle
//     for idle_cycles_before_decommit collection cycles, keeping a few;
//   - a chunk whose pages are all idle and decommitted is unmapped, unless
//     it is the only chunk with free pages.
//
// Pools are size classes: 24 classes up to 2 KiB, shared by every type of
// that size. Blocks above 2 KiB, or with alignment above 16, use the global
// operator new, and so does every block in system mode.
enum class pool_mode : std::uint8_t
{
    size_classes,
    system
};

class block_allocator
{
public:
    static constexpr std::size_t page_size = std::size_t{64} * 1024;
    static constexpr std::size_t pages_per_chunk = 32;
    static constexpr std::size_t chunk_size = page_size * pages_per_chunk;
    static constexpr std::size_t small_align = 16;
    static constexpr std::size_t min_idle_committed_pages = 8;
    static constexpr std::uint32_t idle_cycles_before_decommit = 2;
    static constexpr std::size_t max_pooled_size = 2048;
    static constexpr std::uint16_t large_class = 0xFFFF;
    static constexpr std::uint16_t system_class = 0xFFFE;

    struct allocation
    {
        void* block;
        std::uint16_t pool;
    };

    explicit block_allocator(pool_mode mode = pool_mode::size_classes);
    ~block_allocator();

    block_allocator(const block_allocator&) = delete;
    block_allocator& operator=(const block_allocator&) = delete;

    // Throws std::bad_alloc.
    allocation allocate(std::size_t size, std::size_t align);
    void deallocate(void* block, std::size_t size, std::size_t align, std::uint16_t pool) noexcept;

    // Called at the end of each collection cycle: returns long-idle pages
    // and chunks to the OS.
    void end_cycle() noexcept;

    // Start of the allocated block containing p, or nullptr. Pooled blocks
    // are always found; individually allocated blocks only when debug checks
    // are on (they are tracked only then).
    [[nodiscard]] void* find_block(const void* p) const noexcept;

    // Abandons all chunks without unmapping them, for shutdown with objects
    // that are still referenced from outside.
    void leak() noexcept;

    // Physical memory held: committed pages plus individually allocated blocks.
    [[nodiscard]] std::size_t committed_bytes() const noexcept;
    // Pages currently assigned to a size class.
    [[nodiscard]] std::size_t page_count() const noexcept { return pages_in_use_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunks_.size(); }

private:
    struct page;
    struct chunk;
    struct pool_state
    {
        std::size_t block_size = 0;
        page* available = nullptr; // pages with at least one free block
    };

    page* new_page(std::uint16_t pool);
    void release_page(page* p) noexcept;
    chunk* map_chunk();
    void unmap_chunk(chunk* c) noexcept;
    static void unlink(page*& head, page* p) noexcept;
    static void push(page*& head, page* p) noexcept;
    void unlink_chunk(chunk* c) noexcept;
    void push_chunk(chunk* c) noexcept;

    std::vector<pool_state> pools_;
    std::vector<chunk*> chunks_;          // every mapped chunk
    std::map<std::uintptr_t, chunk*> chunk_index_; // by base address, for find_block
    std::map<std::uintptr_t, std::size_t> individual_blocks_; // debug checks only
    chunk* chunks_with_free_ = nullptr;   // chunks that have at least one free page
    std::size_t pages_in_use_ = 0;
    std::size_t committed_pages_ = 0;     // in use or idle but still committed
    std::size_t idle_committed_pages_ = 0;
    std::size_t individual_bytes_ = 0;
    std::uint32_t cycle_ = 0;
    bool leaked_ = false;
    pool_mode mode_;
};
} // namespace gc::detail
