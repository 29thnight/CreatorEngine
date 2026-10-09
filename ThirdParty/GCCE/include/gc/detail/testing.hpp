#pragma once

#include "../domain.hpp"
#include "../refs.hpp"

#include <cstdint>

namespace gc::detail
{
enum class collection_failure
{
    none,
    gray_reserve,
    suspects_growth,
    quarantine_growth
};

// Hooks for tests that need states reached only after billions of cycles or
// slot reuses. Not part of the public API.
struct domain_testing
{
    GC_API static std::uint32_t mark_epoch(const domain& d) noexcept;
    GC_API static void set_mark_epoch(domain& d, std::uint32_t epoch) noexcept;
    GC_API static std::uint32_t slot_generation(const domain& d, std::uint32_t slot) noexcept;
    GC_API static void set_slot_generation(domain& d, std::uint32_t slot, std::uint32_t generation) noexcept;
    // One-shot failure at the next matching collection allocation point.
    GC_API static void fail_next_collection_allocation(domain& d, collection_failure point) noexcept;

    template <class R>
    static std::uint32_t slot_of(const R& ref) noexcept
    {
        return ref_access::header(ref)->slot;
    }
};
} // namespace gc::detail
