module;
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "module_config.h"

export module Creator.Containers:lock_free_queue;
export import :faa_lock_free_queue;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "faa_lock_free_queue.h"
namespace ce {
// Selected implementation: FAA segments + hazard-protected segment reclamation.
// MaxParticipants bounds registered live threads per T/limit/Hook
// specialization. Cold allocation is outside the core lock-free guarantee;
// prepare/publish splits it.
template <class T, size_t MaxParticipants = 64,
          class Hooks = detail::queue_no_hooks>
using lock_free_queue = faa_lock_free_queue<T, MaxParticipants, Hooks>;
} // namespace ce
} // export
// CE_MODULE_BODY_END
