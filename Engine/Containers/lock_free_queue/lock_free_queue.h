#pragma once
#include "../include/ce/faa_lock_free_queue.h"
namespace ce
{
    // Selected implementation: FAA segments + hazard-protected segment reclamation.
    // MaxParticipants bounds registered live threads per T/limit/Hook
    // specialization. Cold allocation is outside the core lock-free guarantee;
    // prepare/publish splits it.
    template<class T, size_t MaxParticipants = 64, class Hooks = detail::queue_no_hooks>
    using lock_free_queue = faa_lock_free_queue<T, MaxParticipants, Hooks>;
} // namespace ce
