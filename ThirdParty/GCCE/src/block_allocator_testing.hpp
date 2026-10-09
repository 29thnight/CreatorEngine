#pragma once

#include "gc/config.hpp"

#include <cstddef>
#include <cstdint>

namespace gc::detail
{
// Private, owner-thread-only fault injection. Checks are confined to chunk
// growth, page commit and debug-only individual-block tracking, never the
// pooled allocation/free-list fast path.
enum class allocator_failure : std::uint8_t
{
    none,
    chunk_metadata,
    chunk_mapping,
    chunk_vector,
    chunk_index,
    page_commit,
    individual_index
};

struct allocator_test_state
{
    allocator_failure fail_at = allocator_failure::none; // consumed once
    std::size_t chunk_maps = 0;
    std::size_t chunk_unmaps = 0;
};

// Returns the previous state. The caller must restore it before state dies.
GC_API allocator_test_state* set_allocator_test_state(allocator_test_state* state) noexcept;
} // namespace gc::detail
