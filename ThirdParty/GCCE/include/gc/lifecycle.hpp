#pragma once

#include "refs.hpp"

#include <vector>

namespace gc
{
// Logical lifetime queries and transitions. The engine's lifecycle manager
// (M3) drives these; the collector only reads them to detect objects that
// became unreachable before their engine cleanup finished.
//
// All functions require a non-null reference and the owner thread.

template <strong_ref Ref>
[[nodiscard]] lifecycle_state lifecycle_of(const Ref& ref) noexcept
{
    return detail::ref_access::header(ref)->lifecycle;
}

// Moves one step forward: alive -> destroy_requested -> destroying -> destroyed.
// Returns false and changes nothing for any other transition, so repeated
// Destroy requests are ignored. Reaching destroyed discharges the cleanup
// obligation; memory is still reclaimed only when unreachable.
template <strong_ref Ref>
bool advance_lifecycle(const Ref& ref, lifecycle_state next)
{
    return detail::domain_access::advance_lifecycle(detail::ref_access::header(ref), next);
}

// Marks the start of engine registration. From now until destroyed, losing
// reachability is a protocol violation that aborts the collection cycle.
// Returns false if the object is already destroyed.
template <strong_ref Ref>
bool begin_cleanup_obligation(const Ref& ref)
{
    return detail::domain_access::begin_cleanup_obligation(detail::ref_access::header(ref));
}

// Object identity, independent of the static type each reference views.
template <strong_ref A, strong_ref B>
[[nodiscard]] bool same_object(const A& a, const B& b) noexcept
{
    return detail::ref_access::header(a) == detail::ref_access::header(b);
}

// Why is this object alive? Returns the chain from a root (first entry,
// with its label) through strong edges to the object (last entry), or an
// empty vector when it is unreachable. Owner thread; costs a graph walk.
template <strong_ref Ref>
[[nodiscard]] std::vector<object_info> retention_path(const Ref& ref)
{
    const detail::object_header* h = detail::ref_access::header(ref);
    return h ? detail::domain_access::retention_path(h) : std::vector<object_info>{};
}
} // namespace gc
