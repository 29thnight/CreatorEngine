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

export module Creator.Containers:bounded_mpsc_queue;
export import :detail_atomic_u64;
export import :memory;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "detail/atomic_u64.h"
// CE_HEADER_INCLUDE #include "memory.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
namespace ce {
enum class mpsc_push_result { success, unavailable };
struct mpsc_counter_exhausted {};
struct mpsc_invariant_error {};
namespace detail {
struct mpsc_no_hooks {
  static inline void after_ticket() noexcept {}
  static inline void before_publish() noexcept {}
  static inline void after_publish() noexcept {}
  static inline void after_pool_claim() noexcept {}
};
} // namespace detail
// Bounded FAA publication ring + private value pool. Empty reservations may be
// cancelled by the consumer; the delayed producer retries a newer ticket.
// There is no registration or producer-count limit. No TLS/registration or
// dynamically allocated storage. false/unavailable can mean full OR a lost
// concurrent pool probe. It never consumes the argument. Capacity includes
// producer-private values.
template <class T, size_t Capacity, class Hooks = detail::mpsc_no_hooks>
class bounded_mpsc_queue {
  static_assert(Capacity > 0 && Capacity < UINT32_MAX);
  static_assert(noexcept(*static_cast<T *>(nullptr) =
                             static_cast<T &&>(*static_cast<T *>(nullptr))));
  static_assert(detail::nothrow_destroy<T>);
  static_assert(noexcept(Hooks::after_ticket()) &&
                noexcept(Hooks::before_publish()) &&
                noexcept(Hooks::after_publish()) &&
                noexcept(Hooks::after_pool_claim()));
  static constexpr unsigned IndexBits = [] {
    size_t n = Capacity;
    unsigned b = 0;
    do {
      ++b;
      n >>= 1;
    } while (n);
    return b;
  }();
  static constexpr uint64_t IndexMask = (uint64_t(1) << IndexBits) - 1;
  static constexpr uint64_t TicketLimit = [] {
    constexpr uint64_t physical = ((UINT64_MAX >> IndexBits) - 1) * Capacity;
    if constexpr (requires { Hooks::ticket_limit; })
      return uint64_t(Hooks::ticket_limit) < physical
                 ? uint64_t(Hooks::ticket_limit)
                 : physical;
    else
      return physical;
  }();
  static_assert(TicketLimit > 0);
  struct value_slot {
    detail::atomic_u32 owned;
    bool live = false;
    alignas(T) unsigned char bytes[sizeof(T)];
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(bytes)); }
  };
  value_slot values_[Capacity];
  detail::atomic_u64 ring_[Capacity];
  alignas(64) detail::atomic_u64 tail_;
  alignas(64) detail::atomic_u64 pool_cursor_;
  alignas(64) uint64_t head_ = 0;
  detail::atomic_u32 exhausted_;
  value_slot *acquire(size_t &index) noexcept {
    auto start = pool_cursor_.fetch_add(17) % Capacity;
    for (size_t step = 0; step < Capacity; ++step) {
      index = (size_t(start) + step) % Capacity;
      auto &slot = values_[index];
      if (slot.owned.load() != 0)
        continue;
      uint32_t empty = 0;
      if (slot.owned.compare_exchange(empty, 1))
        return &slot;
    }
    return nullptr;
  }

public:
  bounded_mpsc_queue() noexcept = default;
  bounded_mpsc_queue(const bounded_mpsc_queue &) = delete;
  bounded_mpsc_queue &operator=(const bounded_mpsc_queue &) = delete;
  ~bounded_mpsc_queue() {
    for (auto &s : values_)
      if (s.live)
        s.value()->~T();
  }
  static constexpr size_t capacity() noexcept { return Capacity; }
  template <class... A> mpsc_push_result try_emplace_result(A &&...a) {
    if (exhausted_.load())
      throw mpsc_counter_exhausted{};
    size_t index = 0;
    auto slot = acquire(index);
    if (!slot)
      return mpsc_push_result::unavailable;
    Hooks::after_pool_claim(); // private pool slot acquired, before T exists
    try {
      if (exhausted_.load())
        throw mpsc_counter_exhausted{};
      ::new (static_cast<void *>(slot->bytes)) T(static_cast<A &&>(a)...);
      slot->live = true;
      for (;;) {
        auto ticket = tail_.fetch_add(1);
        if (ticket >= TicketLimit) {
          exhausted_.store(1);
          throw mpsc_counter_exhausted{};
        }
        Hooks::after_ticket();
        auto epoch = ticket / Capacity;
        auto expected = epoch << IndexBits;
        Hooks::before_publish(); // ready value; immediately before publication
                                 // CAS
        if (ring_[ticket % Capacity].compare_exchange(
                expected, (epoch << IndexBits) | (index + 1))) {
          Hooks::after_publish();
          return mpsc_push_result::success;
        }
        // A prior epoch cannot remain: each unconsumed ticket owns one of the
        // Capacity private/public values. A newer epoch means consumer
        // progress.
        if ((expected >> IndexBits) < epoch)
          throw mpsc_invariant_error{};
      }
    } catch (...) {
      if (slot->live) {
        slot->value()->~T();
        slot->live = false;
      }
      slot->owned.store(0);
      throw;
    }
  }
  template <class... A> bool try_emplace(A &&...a) {
    return try_emplace_result(static_cast<A &&>(a)...) ==
           mpsc_push_result::success;
  }
  bool try_push(const T &value) { return try_emplace(value); }
  bool try_push(T &&value) { return try_emplace(static_cast<T &&>(value)); }
  bool try_pop(T &out) noexcept {
    auto end = tail_.load();
    if (end > TicketLimit)
      end = TicketLimit;
    while (head_ < end) {
      auto ticket = head_;
      auto epoch = ticket / Capacity;
      auto &word = ring_[ticket % Capacity];
      auto expected = word.load();
      if ((expected >> IndexBits) != epoch)
        ::abort(); // invariant violation, never silently report an empty FIFO
      if (!word.compare_exchange(expected, (epoch + 1) << IndexBits))
        continue;
      ++head_;
      auto index = expected & IndexMask;
      if (!index)
        continue;
      auto &s = values_[size_t(index - 1)];
      out = static_cast<T &&>(*s.value());
      s.value()->~T();
      s.live = false;
      s.owned.store(0);
      return true;
    }
    return false;
  }
};
} // namespace ce
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // export
// CE_MODULE_BODY_END
