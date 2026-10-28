#pragma once
#include <stdint.h>
namespace ce::detail {
enum queue_event {
  allocation,
  registration,
  hazard_first,
  hazard_second,
  link_cas,
  head_cas,
  retire_cas,
  reclaim_total,
  node_release,
  pool_acquire,
  pool_reuse,
  event_count
};
template <class Hooks, queue_event Event> struct queue_profile_scope {
  uint64_t cookie = 0;
  queue_profile_scope() noexcept {
    if constexpr (requires { Hooks::begin(Event); })
      cookie = Hooks::begin(Event);
  }
  ~queue_profile_scope() {
    if constexpr (requires { Hooks::end(Event, cookie); })
      Hooks::end(Event, cookie);
  }
};
template <class Hooks> void queue_profile_failure(queue_event e) noexcept {
  if constexpr (requires { Hooks::failure(e); })
    Hooks::failure(e);
}
} // namespace ce::detail
