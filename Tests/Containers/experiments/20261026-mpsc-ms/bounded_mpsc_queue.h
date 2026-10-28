#pragma once
#include "detail/atomic_ptr.h"
#include "memory.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
namespace ce {
enum class mpsc_push_result { success, full, producer_limit, retry };
namespace detail {
struct mpsc_no_hooks {
  static void after_reserve() noexcept {}
  static void after_protect() noexcept {}
  static void after_link() noexcept {}
  static void after_reclaim_claim() noexcept {}
};
} // namespace detail
// Fixed storage, many producers, exactly one consumer. Michael-Scott linking
// with helping, per-operation hazards and a bounded reusable node pool.
// Capacity credits include private in-flight values. Status distinguishes full
// from producer admission/pool retry; false does not always mean public-full.
// No allocator, TLS registration, mutex, spinlock or owner-dependent wait.
template <class T, size_t Capacity, size_t MaxProducers = 32,
          class Hooks = detail::mpsc_no_hooks>
class bounded_mpsc_queue {
  static_assert(Capacity > 0 && MaxProducers > 0);
  static_assert(Capacity <= UINT32_MAX - MaxProducers - 2);
  static_assert(noexcept(*static_cast<T *>(nullptr) =
                             static_cast<T &&>(*static_cast<T *>(nullptr))));
  static_assert(detail::nothrow_destroy<T>);
  static_assert(noexcept(Hooks::after_reserve()) &&
                noexcept(Hooks::after_protect()) &&
                noexcept(Hooks::after_link()) &&
                noexcept(Hooks::after_reclaim_claim()));
  struct node {
    detail::atomic_ptr<node> next;
    // 0 free, 1 owned/linked, 2 retired, 3 reclamation claim.
    detail::atomic_u32 state;
    bool live = false;
    alignas(T) unsigned char bytes[sizeof(T)];
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(bytes)); }
  };
  struct alignas(64) participant {
    detail::atomic_u32 active;
    detail::atomic_ptr<node> hazard;
    size_t cursor=0; // exclusively owned while this participant is admitted
  };
  static constexpr size_t Nodes = Capacity + MaxProducers + 2;
  node nodes_[Nodes];
  participant producers_[MaxProducers];
  alignas(64) detail::atomic_ptr<node> tail_;
  alignas(64) node *head_;
  alignas(64) detail::atomic_u32 credits_;
  struct operation {
    participant *p = nullptr;
    ~operation() {
      if (p) {
        p->hazard.store_release(nullptr);
        p->active.store(0);
      }
    }
  };
  participant *admit() noexcept {
    for (auto &p : producers_) {
      if(p.active.load()!=0) continue;
      uint32_t expected = 0;
      if (p.active.compare_exchange(expected, 1))
        return &p;
    }
    return nullptr;
  }
  bool reclaim_for_push(node &n) noexcept {
    uint32_t expected = 2;
    if (!n.state.compare_exchange(expected, 3))
      return false;
    Hooks::after_reclaim_claim();
    for (auto &p : producers_)
      if (p.hazard.load() == &n) {
        n.state.store(2);
        return false;
      }
    // Keep the exclusive claim. Publishing an intermediate free state would
    // require a second contended CAS merely to reacquire the same node.
    n.state.store(1);
    return true;
  }
  node *acquire_node(participant& producer) noexcept {
    size_t start = producer.cursor;
    for (size_t step = 0; step < Nodes; ++step) {
      auto index=(start + step) % Nodes;
      auto &n = nodes_[index];
      if (n.state.load() == 2 && reclaim_for_push(n)) {
        producer.cursor=(index+1)%Nodes;
        return &n;
      }
      uint32_t expected = 0;
      if (n.state.compare_exchange(expected, 1)) {
        producer.cursor=(index+1)%Nodes;
        return &n;
      }
    }
    return nullptr;
  }

public:
  bounded_mpsc_queue() noexcept : tail_(&nodes_[0]), head_(&nodes_[0]) {
    nodes_[0].state.store(1);
    for(size_t i=0;i<MaxProducers;++i)producers_[i].cursor=(i+1)%Nodes;
  }
  bounded_mpsc_queue(const bounded_mpsc_queue &) = delete;
  bounded_mpsc_queue &operator=(const bounded_mpsc_queue &) = delete;
  ~bounded_mpsc_queue() {
    // Quiescence required; no simultaneous queue operation may outlive this.
    for (auto &n : nodes_)
      if (n.live)
        n.value()->~T();
  }
  static constexpr size_t capacity() noexcept { return Capacity; }
  template <class... A> mpsc_push_result try_emplace_result(A &&...a) {
    operation op{admit()};
    if (!op.p)
      return mpsc_push_result::producer_limit;
    auto count = credits_.load();
    for (;;) {
      if (count >= Capacity)
        return mpsc_push_result::full;
      if (credits_.compare_exchange(count, count + 1))
        break;
    }
    Hooks::after_reserve();
    auto fresh = acquire_node(*op.p);
    if (!fresh) {
      credits_.fetch_add(uint32_t(-1));
      return mpsc_push_result::retry;
    }
    try {
      ::new (static_cast<void *>(fresh->bytes)) T(static_cast<A &&>(a)...);
    } catch (...) {
      fresh->state.store(0);
      credits_.fetch_add(uint32_t(-1));
      throw;
    }
    fresh->live = true;
    fresh->next.store_release(nullptr);
    for (;;) {
      auto tail = tail_.load();
      op.p->hazard.store(tail);
      if (tail != tail_.load())
        continue;
      Hooks::after_protect();
      auto next = tail->next.load();
      if (tail != tail_.load())
        continue;
      if (next) {
        tail_.compare_exchange(tail, next);
        continue;
      }
      node *empty = nullptr;
      if (tail->next.compare_exchange(empty, fresh)) {
        Hooks::after_link();
        tail_.compare_exchange(tail, fresh);
        return mpsc_push_result::success;
      }
    }
  }
  template <class... A> bool try_emplace(A &&...a) {
    return try_emplace_result(static_cast<A &&>(a)...) ==
           mpsc_push_result::success;
  }
  bool try_push(const T &value) { return try_emplace(value); }
  bool try_push(T &&value) { return try_emplace(static_cast<T &&>(value)); }
  bool try_pop(T &out) noexcept {
    auto old = head_;
    auto next = old->next.load();
    if (!next)
      return false;
    auto tail = tail_.load();
    if (tail == old)
      tail_.compare_exchange(tail, next);
    out = static_cast<T &&>(*next->value());
    next->value()->~T();
    next->live = false;
    head_ = next;
    old->state.store(2);
    credits_.fetch_add(uint32_t(-1));
    // Reclamation belongs to producers. A stopped consumer cannot own a
    // reclamation claim or prevent producer-side recycling.
    return true;
  }
};
} // namespace ce
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
