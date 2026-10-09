#pragma once

#include <cstddef>
#include <cstdint>

namespace gc
{
class domain;
class tracer;

// Logical (engine) lifetime. Kept separate from GC mark state and slot
// generation: a destroyed object can still be reachable and is still traced.
enum class lifecycle_state : std::uint8_t
{
    alive,
    destroy_requested,
    destroying,
    destroyed
};

namespace detail
{
struct type_descriptor
{
    const char* name;
    std::size_t size;
    std::size_t align;
    void (*trace)(const void* object, tracer& t);
    void (*destroy)(void* object) noexcept;
};

// Placed at the start of every GC block, followed by the object storage.
struct object_header
{
    domain* owner = nullptr;
    const type_descriptor* type = nullptr;
    void* object = nullptr;
    std::size_t block_size = 0;
    std::size_t block_align = 0;
    std::uint64_t alloc_serial = 0;
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    // Marked in the current cycle when equal to the domain's mark epoch.
    // Gray and black are not distinguished: the insertion barrier shades the
    // target regardless of the color of the object holding the reference.
    std::uint32_t mark_epoch = 0;
    std::uint16_t pool = 0; // allocator bookkeeping
    lifecycle_state lifecycle = lifecycle_state::alive;
    // Set once engine registration starts; cleared on reaching destroyed.
    bool cleanup_obligation = false;
    // Held by the domain's quarantine after a cleanup-obligation violation.
    bool quarantined = false;
};

// Intrusive node of the domain's root list, embedded in every root_ref.
struct root_node
{
    root_node* prev = nullptr;
    root_node* next = nullptr;
    object_header* header = nullptr;
    // Diagnostic name of whoever holds the root. Belongs to the root_ref
    // object, not to its value: never copied or moved.
    const char* label = nullptr;
};
} // namespace detail
} // namespace gc
