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

export module Creator.Containers:faa_lock_free_queue;
export import :detail_atomic_ptr;
export import :detail_queue_profile;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "detail/atomic_ptr.h"
// CE_HEADER_INCLUDE #include "detail/queue_profile.h"
// CE_HEADER_INCLUDE #include <new>
// CE_HEADER_INCLUDE #include <stddef.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
namespace ce {
struct queue_operation_limit {};
struct queue_token_mismatch {};
namespace detail {
struct queue_no_hooks {
  static inline void after_link() noexcept {}
  static inline void after_head_protected() noexcept {}
  static inline void node_created() noexcept {}
  static inline void node_destroyed() noexcept {};
};
} // namespace detail
// FAA segmented FIFO: skipped empty reservations are permanently marked taken;
// a late publisher retries elsewhere instead of holding up other participants.
// Normal cache-hit push/pop are allocation-free. Cold allocation can block in
// CRT. Exclusive exchange of whole cache batches avoids a freelist ABA
// protocol.
template <class T, size_t MaxParticipants = 64,
          class Hooks = detail::queue_no_hooks>
class faa_lock_free_queue {
  static_assert(MaxParticipants > 0 && MaxParticipants <= 4096);
  static_assert(noexcept(*static_cast<T *>(nullptr) =
                             static_cast<T &&>(*static_cast<T *>(nullptr))));
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  static_assert(noexcept(Hooks::after_link()) &&
                noexcept(Hooks::after_head_protected()) &&
                noexcept(Hooks::node_created()) &&
                noexcept(Hooks::node_destroyed()));
  static constexpr uint32_t Slots = 256;
  struct value_node {
    uintptr_t link_flags =
        0; // private ownership metadata; low bits: live, slab
    alignas(T) unsigned char bytes[sizeof(T)];
    value_node() noexcept { Hooks::node_created(); }
    value_node *next() const noexcept {
      return reinterpret_cast<value_node *>(link_flags & ~uintptr_t(3));
    }
    void set_next(value_node *p) noexcept {
      link_flags = (link_flags & 3) | reinterpret_cast<uintptr_t>(p);
    }
    bool is_live() const noexcept { return (link_flags & 1) != 0; }
    bool is_slab() const noexcept { return (link_flags & 2) != 0; }
    void set_live(bool b) noexcept {
      if (b)
        link_flags |= 1;
      else
        link_flags &= ~uintptr_t(1);
    }
    void set_slab() noexcept { link_flags |= 2; }
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(bytes)); }
    template <class... A> void construct(A &&...a) {
      ::new (static_cast<void *>(bytes)) T(static_cast<A &&>(a)...);
      set_live(true);
    }
    ~value_node() {
      if (is_live())
        value()->~T();
      Hooks::node_destroyed();
    }
  };
  static_assert(alignof(value_node) >= 4);
  struct segment {
    alignas(64) detail::atomic_u32 enq;
    alignas(64) detail::atomic_u32 deq;
    alignas(64) detail::atomic_ptr<segment> next;
    detail::atomic_ptr<value_node> cells[Slots];
    segment *retired_next = nullptr;
    explicit segment(value_node *first = nullptr) noexcept {
      if (first) {
        cells[0].store_release(first);
        enq.store(1);
      }
      if constexpr (requires { Hooks::segment_created(); })
        Hooks::segment_created();
    }
    ~segment() {
      if constexpr (requires { Hooks::segment_destroyed(); })
        Hooks::segment_destroyed();
    }
    static uint32_t map(uint32_t n) noexcept {
      return (n & 7) * (Slots / 8) + (n >> 3);
    }
  };
  static constexpr size_t BlockSlots = sizeof(value_node) <= 32 ? 512 : 64;
  struct value_block {
    value_block *next = nullptr;
    value_node nodes[BlockSlots];
  };
  struct alignas(64) record {
    detail::atomic_u32 touched, producer;
    detail::atomic_ptr<segment> hazard;
    detail::atomic_ptr<value_node> incoming;
    value_node *cache = nullptr, *retired_values = nullptr;
    segment *retired_segments = nullptr;
    segment *cached_head = nullptr;
    uint32_t cached_enq = 0;
  };
  alignas(64) detail::atomic_ptr<segment> head_;
  alignas(64) detail::atomic_ptr<segment> tail_;
  alignas(64) detail::atomic_ptr<segment> deferred_segments_;
  alignas(64) detail::atomic_ptr<value_block> blocks_;
  alignas(64) detail::atomic_ptr<value_node> pending_values_;
  mutable record records_[MaxParticipants];
  detail::atomic_u32 has_individual_;
  alignas(value_node) inline static unsigned char marker_;
  static value_node *taken() noexcept {
    return reinterpret_cast<value_node *>(&marker_);
  }
  // Registration belongs to the thread/type, never to a queue object's
  // lifetime. TLS teardown touches only this static domain, so no
  // destroyed-queue callback.
  inline static detail::atomic_u32 leases_[MaxParticipants];
  struct thread_lease;
  inline static thread_local thread_lease *current_ = nullptr;
  struct thread_lease {
    size_t index = 0;
    thread_lease() {
      for (size_t i = 0; i < MaxParticipants; ++i) {
        uint32_t e = 0;
        if (leases_[i].compare_exchange(e, 1)) {
          index = i;
          current_ = this;
          return;
        }
      }
      throw queue_operation_limit{};
    }
    ~thread_lease() {
      current_ = nullptr;
      leases_[index].store(0);
    }
  };
  static size_t thread_index() {
    static thread_local thread_lease lease;
    return lease.index;
  }
  class operation {
    record *r_;
    size_t index_;

  public:
    explicit operation(const faa_lock_free_queue &q) : index_(0) {
      detail::queue_profile_scope<Hooks, detail::registration> profile;
      index_ = thread_index();
      r_ = &q.records_[index_];
      if (!r_->touched.load())
        r_->touched.store(1);
    }
    segment *protect(const detail::atomic_ptr<segment> &source) {
      detail::queue_profile_scope<Hooks, detail::hazard_first> profile;
      segment *p;
      do {
        p = source.load();
        if (r_->hazard.load() != p) {
          r_->cached_head = nullptr;
          r_->cached_enq = 0;
          r_->hazard.store(p);
        }
      } while (source.load() != p);
      return p;
    }
    record &local() noexcept { return *r_; }
    size_t index() const noexcept { return index_; }
    operation(const operation &) = delete;
    operation &operator=(const operation &) = delete;
  };
  value_node *acquire(operation &op) {
    detail::queue_profile_scope<Hooks, detail::pool_acquire> profile;
    auto &r = op.local();
    if (!r.producer.load())
      r.producer.store(1);
    if (!r.cache) {
      for (size_t i = 0; i < MaxParticipants; ++i) {
        auto &source = records_[(op.index() + i) % MaxParticipants].incoming;
        if (!source.load())
          continue;
        r.cache = source.exchange(nullptr);
        if (r.cache)
          break;
      }
    }
    auto p = r.cache;
    if (p) {
      detail::queue_profile_scope<Hooks, detail::pool_reuse> reuse;
      r.cache = p->next();
      return p;
    }
    detail::queue_profile_scope<Hooks, detail::allocation> allocate;
    auto block = new value_block;
    auto old = blocks_.load();
    do {
      block->next = old;
    } while (!blocks_.compare_exchange(old, block));
    for (size_t i = 0; i < BlockSlots; ++i) {
      auto cell = &block->nodes[i];
      cell->set_slab();
      cell->set_next(r.cache);
      r.cache = cell;
    }
    auto fresh = r.cache;
    r.cache = fresh->next();
    return fresh;
  }
  void enqueue(value_node *p, operation &op, segment **reserve = nullptr) {
    for (;;) {
      auto tail = op.protect(tail_);
      auto index = tail->enq.load();
      if (index < Slots)
        index = tail->enq.fetch_add(1);
      if (index < Slots) {
        if constexpr (requires { Hooks::after_reserve(); })
          Hooks::after_reserve();
        value_node *e = nullptr;
        bool published;
        {
          detail::queue_profile_scope<Hooks, detail::link_cas> profile;
          published = tail->cells[segment::map(index)].compare_exchange(e, p);
        }
        if (published) {
          Hooks::after_link();
          return;
        }
        detail::queue_profile_failure<Hooks>(detail::link_cas);
        continue;
      }
      if (tail_.load() != tail)
        continue;
      auto next = tail->next.load();
      if (next) {
        tail_.compare_exchange(tail, next);
        continue;
      }
      segment *fresh;
      {
        detail::queue_profile_scope<Hooks, detail::allocation> allocate;
        fresh = reserve ? *reserve : new segment(p);
      }
      segment *e = nullptr;
      if (tail->next.compare_exchange(e, fresh)) {
        if (reserve)
          *reserve = nullptr;
        Hooks::after_link();
        tail_.compare_exchange(tail, fresh);
        return;
      }
      if (!reserve)
        delete fresh;
    }
  }
  template <class... A> void append_with(operation &op, A &&...a) {
    auto p = acquire(op);
    try {
      p->construct(static_cast<A &&>(a)...);
      enqueue(p, op);
    } catch (...) {
      if (p->is_live()) {
        p->value()->~T();
        p->set_live(false);
      }
      p->set_next(op.local().cache);
      op.local().cache = p;
      throw;
    }
  }
  template <class... A> void append(A &&...a) {
    operation op(*this);
    append_with(op, static_cast<A &&>(a)...);
  }
  void flush(operation &op) noexcept {
    auto &r = op.local();
    if (r.retired_values) {
      auto first = r.retired_values;
      r.retired_values = nullptr;
      auto last = first;
      while (last->next())
        last = last->next();
      auto old = pending_values_.load();
      do {
        last->set_next(old);
      } while (!pending_values_.compare_exchange(old, first));
    }
    if (r.retired_segments) {
      auto first = r.retired_segments;
      r.retired_segments = nullptr;
      auto last = first;
      while (last->retired_next)
        last = last->retired_next;
      auto old = deferred_segments_.load();
      do {
        last->retired_next = old;
      } while (!deferred_segments_.compare_exchange(old, first));
    }
  }
  size_t collect(bool reuse) noexcept {
    detail::queue_profile_scope<Hooks, detail::reclaim_total> profile;
    value_node *values = pending_values_.exchange(nullptr);
    segment *segments = deferred_segments_.exchange(nullptr);
    // Borrow idle records without waiting. Never read their non-atomic lists
    // until CAS succeeds.
    for (size_t ix = 0; ix < MaxParticipants; ++ix) {
      auto &r = records_[ix];
      if (!r.touched.load())
        continue;
      bool own = current_ && current_->index == ix;
      if (!own) {
        uint32_t e = 0;
        if (!leases_[ix].compare_exchange(e, 2))
          continue;
      }
      r.hazard.store_release(nullptr);
      r.cached_head = nullptr;
      r.cached_enq = 0;
      if (r.retired_values) {
        auto p = r.retired_values;
        r.retired_values = nullptr;
        while (p) {
          auto n = p->next();
          p->set_next(values);
          values = p;
          p = n;
        }
      }
      if (r.retired_segments) {
        auto p = r.retired_segments;
        r.retired_segments = nullptr;
        while (p) {
          auto n = p->retired_next;
          p->retired_next = segments;
          segments = p;
          p = n;
        }
      }
      if (!own)
        leases_[ix].store(0);
    }
    segment *hazards[MaxParticipants];
    size_t hc = 0;
    for (auto &r : records_) {
      auto p = r.hazard.load();
      if (p)
        hazards[hc++] = p;
    }
    size_t count = 0;
    segment *held = nullptr;
    while (segments) {
      auto p = segments;
      segments = p->retired_next;
      bool protect = false;
      for (size_t i = 0; i < hc; ++i)
        if (hazards[i] == p) {
          protect = true;
          break;
        }
      if (protect) {
        p->retired_next = held;
        held = p;
      } else {
        delete p;
        ++count;
      }
    }
    if (held) {
      auto last = held;
      while (last->retired_next)
        last = last->retired_next;
      auto old = deferred_segments_.load();
      do {
        last->retired_next = old;
      } while (!deferred_segments_.compare_exchange(old, held));
    }
    value_node *ready[MaxParticipants]{}, *last[MaxParticipants]{};
    static thread_local size_t destination = 0;
    size_t batch = 0;
    while (values) {
      auto p = values;
      values = p->next();
      if (reuse || p->is_slab()) {
        if (batch % 64 == 0) {
          for (size_t j = 0; j < MaxParticipants; ++j) {
            destination = (destination + 1) % MaxParticipants;
            if (records_[destination].producer.load())
              break;
          }
        }
        ++batch;
        auto h = destination;
        p->set_next(ready[h]);
        ready[h] = p;
        if (!last[h])
          last[h] = p;
      } else {
        detail::queue_profile_scope<Hooks, detail::node_release> release;
        if (!p->is_slab())
          delete p;
      }
      ++count;
    }
    for (size_t i = 0; i < MaxParticipants; ++i)
      if (ready[i]) {
        auto &list = records_[i].incoming;
        auto old = list.load();
        do {
          last[i]->set_next(old);
        } while (!list.compare_exchange(old, ready[i]));
      }
    return count;
  }
  void destroy_quiescent() noexcept {
    if (!has_individual_.load()) {
      // Every value is already owned by blocks_: do not walk every free-list
      // entry a second time. The block destructors destroy any still-live T
      // exactly once.
      auto seg = head_.exchange(nullptr);
      tail_.store(nullptr);
      while (seg) {
        auto next = seg->next.load();
        delete seg;
        seg = next;
      }
      seg = deferred_segments_.exchange(nullptr);
      while (seg) {
        auto next = seg->retired_next;
        delete seg;
        seg = next;
      }
      for (auto &r : records_) {
        r.incoming.store_release(nullptr);
        r.cache = nullptr;
        r.retired_values = nullptr;
        seg = r.retired_segments;
        r.retired_segments = nullptr;
        while (seg) {
          auto next = seg->retired_next;
          delete seg;
          seg = next;
        }
        r.hazard.store_release(nullptr);
        r.cached_head = nullptr;
        r.cached_enq = 0;
      }
      pending_values_.store_release(nullptr);
      auto block = blocks_.exchange(nullptr);
      while (block) {
        auto next = block->next;
        delete block;
        block = next;
      }
      return;
    }
    auto seg = head_.exchange(nullptr);
    tail_.store(nullptr);
    while (seg) {
      auto next = seg->next.load();
      for (auto &cell : seg->cells) {
        auto p = cell.load();
        if (p && p != taken())
          if (!p->is_slab())
            delete p;
      }
      delete seg;
      seg = next;
    }
    seg = deferred_segments_.exchange(nullptr);
    while (seg) {
      auto next = seg->retired_next;
      delete seg;
      seg = next;
    }
    for (auto &r : records_) {
      auto p = r.incoming.exchange(nullptr);
      while (p) {
        auto n = p->next();
        if (!p->is_slab())
          delete p;
        p = n;
      }
      p = r.cache;
      r.cache = nullptr;
      while (p) {
        auto n = p->next();
        if (!p->is_slab())
          delete p;
        p = n;
      }
      p = r.retired_values;
      r.retired_values = nullptr;
      while (p) {
        auto n = p->next();
        if (!p->is_slab())
          delete p;
        p = n;
      }
      seg = r.retired_segments;
      r.retired_segments = nullptr;
      while (seg) {
        auto n = seg->retired_next;
        delete seg;
        seg = n;
      }
      r.hazard.store_release(nullptr);
      r.cached_head = nullptr;
      r.cached_enq = 0;
    }
    auto pending = pending_values_.exchange(nullptr);
    while (pending) {
      auto n = pending->next();
      if (!pending->is_slab())
        delete pending;
      pending = n;
    }
    auto block = blocks_.exchange(nullptr);
    while (block) {
      auto next = block->next;
      delete block;
      block = next;
    }
    has_individual_.store(0);
  }

  bool pop_with(T &out, operation &op) {
    for (;;) {
      auto head = op.protect(head_);
      Hooks::after_head_protected();
      auto d = head->deq.load();
      auto &r = op.local();
      if (r.cached_head != head) {
        r.cached_head = head;
        r.cached_enq = 0;
      }
      if (d >= r.cached_enq) {
        r.cached_enq = head->enq.load();
        if (d >= r.cached_enq && !head->next.load())
          return false;
      }
      if (d < Slots)
        d = head->deq.fetch_add(1);
      if (d >= Slots) {
        auto next = head->next.load();
        if (!next)
          return false;
        bool moved;
        {
          detail::queue_profile_scope<Hooks, detail::head_cas> profile;
          moved = head_.compare_exchange(head, next);
        }
        if (moved) {
          head->retired_next = op.local().retired_segments;
          op.local().retired_segments = head;
        }
        continue;
      }
      value_node *p;
      {
        detail::queue_profile_scope<Hooks, detail::head_cas> profile;
        p = head->cells[segment::map(d)].exchange(taken());
      }
      if (!p)
        continue;
      out = static_cast<T &&>(*p->value());
      p->value()->~T();
      p->set_live(false);
      p->set_next(op.local().retired_values);
      op.local().retired_values = p;
      return true;
    }
  }

public:
  class access_token {
    friend class faa_lock_free_queue;
    const faa_lock_free_queue *owner_;
    operation op_;
    explicit access_token(const faa_lock_free_queue &q) : owner_(&q), op_(q) {}

  public:
    access_token(const access_token &) = delete;
    access_token &operator=(const access_token &) = delete;
  };
  // Call once during worker setup if cold TLS registration must be outside a
  // timed region.
  static void register_current_thread() { (void)thread_index(); }
  access_token make_token() const { return access_token(*this); }
  void push(access_token &t, const T &v) {
    if (t.owner_ != this || t.op_.index() != thread_index())
      throw queue_token_mismatch{};
    append_with(t.op_, v);
  }
  void push(access_token &t, T &&v) {
    if (t.owner_ != this || t.op_.index() != thread_index())
      throw queue_token_mismatch{};
    append_with(t.op_, static_cast<T &&>(v));
  }
  bool try_pop(access_token &t, T &out) {
    if (t.owner_ != this || t.op_.index() != thread_index())
      throw queue_token_mismatch{};
    return pop_with(out, t.op_);
  }
  size_t reclaim(access_token &t) {
    if (t.owner_ != this || t.op_.index() != thread_index())
      throw queue_token_mismatch{};
    flush(t.op_);
    return collect(true);
  }
  class prepared_node {
    value_node *p_ = nullptr;
    segment *spare_ = nullptr;
    friend class faa_lock_free_queue;
    prepared_node(value_node *p, segment *s) noexcept : p_(p), spare_(s) {}

  public:
    prepared_node() noexcept = default;
    prepared_node(prepared_node &&o) noexcept : p_(o.p_), spare_(o.spare_) {
      o.p_ = nullptr;
      o.spare_ = nullptr;
    }
    prepared_node &operator=(prepared_node &&o) noexcept {
      if (this != &o) {
        delete p_;
        delete spare_;
        p_ = o.p_;
        spare_ = o.spare_;
        o.p_ = nullptr;
        o.spare_ = nullptr;
      }
      return *this;
    }
    prepared_node(const prepared_node &) = delete;
    prepared_node &operator=(const prepared_node &) = delete;
    ~prepared_node() {
      delete p_;
      delete spare_;
    }
    explicit operator bool() const noexcept { return p_ != nullptr; }
  };
  static constexpr size_t block_slot_count = BlockSlots;
  static constexpr size_t value_storage_bytes = sizeof(value_node),
                          segment_storage_bytes = sizeof(segment),
                          block_storage_bytes = sizeof(value_block);
  faa_lock_free_queue() {
    auto p = new segment;
    head_.store(p);
    tail_.store(p);
  }
  ~faa_lock_free_queue() { destroy_quiescent(); }
  faa_lock_free_queue(const faa_lock_free_queue &) = delete;
  faa_lock_free_queue &operator=(const faa_lock_free_queue &) = delete;
  template <class... A> static prepared_node prepare(A &&...a) {
    detail::queue_profile_scope<Hooks, detail::allocation> profile;
    auto p = new value_node;
    try {
      p->construct(static_cast<A &&>(a)...);
    } catch (...) {
      delete p;
      throw;
    }
    segment *spare;
    try {
      spare = new segment(p);
    } catch (...) {
      delete p;
      throw;
    }
    return prepared_node(p, spare);
  }
  bool publish(prepared_node &&p) {
    if (!p.p_)
      return false;
    operation op(*this);
    if (!has_individual_.load())
      has_individual_.store(1);
    enqueue(p.p_, op, &p.spare_);
    p.p_ = nullptr;
    return true;
  }
  void push(const T &v) { append(v); }
  void push(T &&v) { append(static_cast<T &&>(v)); }
  template <class... A> void emplace(A &&...a) {
    append(static_cast<A &&>(a)...);
  }
  bool try_pop(T &out) {
    operation op(*this);
    return pop_with(out, op);
  }
  bool empty() const {
    operation op(*this);
    for (;;) {
      auto head = op.protect(head_);
      auto d = head->deq.load(), e = head->enq.load();
      for (uint32_t i = d; i < e && i < Slots; ++i) {
        auto p = head->cells[segment::map(i)].load();
        if (p && p != taken())
          return false;
      }
      auto next = head->next.load();
      if (head_.load() != head || head->deq.load() != d ||
          head->enq.load() != e)
        continue;
      if (next)
        return false;
      return true;
    }
  }
  size_t reclaim() noexcept { return collect(true); }
  size_t release_retired() noexcept { return collect(false); }
  void clear() {
    auto fresh = new segment;
    destroy_quiescent();
    head_.store(fresh);
    tail_.store(fresh);
  }
};
} // namespace ce
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // export
// CE_MODULE_BODY_END
