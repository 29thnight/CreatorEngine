#pragma once
#include "detail/atomic_ptr.h"
#include "detail/queue_profile.h"
#include <new>
#include <stddef.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // deliberate alignment of node payload storage
#endif
namespace ce {
struct queue_operation_limit {};
namespace detail {
struct queue_no_hooks {
  static void after_link() noexcept {}
  static void after_head_protected() noexcept {}
  static void node_created() noexcept {}
  static void node_destroyed() noexcept {}
};
} // namespace detail
// Michael-Scott MPMC links + hazard pointers. No queue lock or owner wait.
// prepare/push allocate; reclaim deletes; allocator progress is NOT guaranteed.
// publish(prepared&&) and try_pop do no allocation/reclamation themselves.
// MaxOperations limits simultaneously ACTIVE calls, not the lifetime thread
// count. T move-assignment/destruction must be noexcept and must not reenter
// this queue. clear/destruction require quiescence. reclaim() may run
// concurrently.
template <class T, size_t MaxOperations = 64,
          class Hooks = detail::queue_no_hooks>
class lock_free_queue {
  static_assert(MaxOperations > 0 && MaxOperations <= 4096);
  static_assert(noexcept(*static_cast<T *>(nullptr) =
                             static_cast<T &&>(*static_cast<T *>(nullptr))));
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  struct node {
    detail::atomic_ptr<node> next;
    node *retired_next = nullptr;
    bool live = false;
    alignas(T) unsigned char storage[sizeof(T)];
    node() noexcept { Hooks::node_created(); }
    template <class... Args> node(int, Args &&...args) {
      ::new (static_cast<void *>(storage)) T(static_cast<Args &&>(args)...);
      live = true;
      Hooks::node_created();
    }
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(storage)); }
    ~node() {
      if (live)
        value()->~T();
      Hooks::node_destroyed();
    }
  };
  struct alignas(64) hazard_record {
    detail::atomic_u32 busy;
    detail::atomic_ptr<node> first, second;
  };
  alignas(64) detail::atomic_ptr<node> head_;
  alignas(64) detail::atomic_ptr<node> tail_;
  alignas(64) detail::atomic_ptr<node> retired_;
  mutable hazard_record records_[MaxOperations];
  class operation {
    const lock_free_queue &q_;
    hazard_record *record_ = nullptr;

  public:
    explicit operation(const lock_free_queue &q) : q_(q) {
      detail::queue_profile_scope<Hooks,detail::registration> profile;
      static thread_local size_t preferred = 0;
      for (size_t i = 0; i < MaxOperations; ++i) {
        auto index = (preferred + i) % MaxOperations;
        uint32_t expected = 0;
        if (q_.records_[index].busy.compare_exchange(expected, 1)) {
          record_ = &q_.records_[index];
          preferred = index;
          return;
        }
      }
      throw queue_operation_limit{};
    }
    ~operation() {
      record_->second.store(nullptr);
      record_->first.store(nullptr);
      record_->busy.store(0);
    }
    node *protect(const detail::atomic_ptr<node> &source) {
      detail::queue_profile_scope<Hooks,detail::hazard_first> profile;
      node *p;
      do {
        p = source.load();
        record_->first.store(p);
      } while (source.load() != p);
      return p;
    }
    void protect_second(node *p) { detail::queue_profile_scope<Hooks,detail::hazard_second> profile;record_->second.store(p); }
    operation(const operation &) = delete;
    operation &operator=(const operation &) = delete;
  };
  void retire(node *p) noexcept {
    detail::queue_profile_scope<Hooks,detail::retire_cas> profile;
    auto old = retired_.load();
    do {
      p->retired_next = old;
    } while (!retired_.compare_exchange(old, p));
  }
  void destroy_quiescent() noexcept {
    auto p = head_.exchange(nullptr);
    tail_.store(nullptr);
    while (p) {
      auto n = p->next.load();
      delete p;
      p = n;
    }
    p = retired_.exchange(nullptr);
    while (p) {
      auto n = p->retired_next;
      delete p;
      p = n;
    }
  }

public:
  class prepared_node {
    node *p_ = nullptr;
    friend class lock_free_queue;
    explicit prepared_node(node *p) noexcept : p_(p) {}

  public:
    prepared_node() noexcept = default;
    prepared_node(prepared_node &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    prepared_node &operator=(prepared_node &&o) noexcept {
      if (this != &o) {
        delete p_;
        p_ = o.p_;
        o.p_ = nullptr;
      }
      return *this;
    }
    prepared_node(const prepared_node &) = delete;
    prepared_node &operator=(const prepared_node &) = delete;
    ~prepared_node() { delete p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
  };
  lock_free_queue() {
    auto dummy = new node;
    head_.store(dummy);
    tail_.store(dummy);
  }
  ~lock_free_queue() { destroy_quiescent(); }
  lock_free_queue(const lock_free_queue &) = delete;
  lock_free_queue &operator=(const lock_free_queue &) = delete;
  template <class... Args> static prepared_node prepare(Args &&...args) {
    detail::queue_profile_scope<Hooks,detail::allocation> profile;
    return prepared_node(new node(0, static_cast<Args &&>(args)...));
  }
  // false only for an empty handle; record exhaustion throws before
  // publication.
  bool publish(prepared_node &&prepared) {
    if (!prepared.p_)
      return false;
    operation op(*this);
    auto fresh = prepared.p_;
    for (;;) {
      auto tail = op.protect(tail_);
      auto next = tail->next.load();
      if (tail_.load() != tail)
        continue;
      if (next) {
        tail_.compare_exchange(tail, next);
        continue;
      }
      node *expected = nullptr;
      bool linked;{detail::queue_profile_scope<Hooks,detail::link_cas> profile;linked=tail->next.compare_exchange(expected,fresh);}
      if (linked) {
        prepared.p_ = nullptr; // ownership transferred at the link CAS
        Hooks::after_link();   // test hook: another thread must be able to help
                               // tail
        tail_.compare_exchange(tail, fresh);
        return true;
      }else detail::queue_profile_failure<Hooks>(detail::link_cas);
    }
  }
  void push(const T &x) {
    auto p = prepare(x);
    publish(static_cast<prepared_node &&>(p));
  }
  void push(T &&x) {
    auto p = prepare(static_cast<T &&>(x));
    publish(static_cast<prepared_node &&>(p));
  }
  template <class... Args> void emplace(Args &&...args) {
    auto p = prepare(static_cast<Args &&>(args)...);
    publish(static_cast<prepared_node &&>(p));
  }
  bool try_pop(T &out) {
    operation op(*this);
    for (;;) {
      auto head = op.protect(head_);
      Hooks::after_head_protected();
      auto next = head->next.load();
      op.protect_second(next);
      if (head_.load() != head)
        continue;
      if (!next)
        return false;
      auto tail = tail_.load();
      if (head == tail) {
        tail_.compare_exchange(tail, next);
        continue;
      }
      bool removed;{detail::queue_profile_scope<Hooks,detail::head_cas> profile;removed=head_.compare_exchange(head,next);}
      if (removed) {
        out = static_cast<T &&>(*next->value());
        next->value()->~T();
        next->live = false;
        retire(head);
        return true;
      }else detail::queue_profile_failure<Hooks>(detail::head_cas);
    }
  }
  bool empty() const {
    operation op(*this);
    for (;;) {
      auto head = op.protect(head_);
      auto next = head->next.load();
      if (head_.load() == head)
        return next == nullptr;
    }
  }
  // Detach first, then take an SC hazard snapshot: protects stale readers/ABA.
  // Memory release/allocator cost is explicitly outside publish/try_pop.
  size_t reclaim() noexcept {
    detail::queue_profile_scope<Hooks,detail::reclaim_total> profile;
    auto list = retired_.exchange(nullptr);
    if (!list)
      return 0;
    node *hazards[MaxOperations * 2];
    size_t count = 0;
    for (auto &r : records_) {
      auto a = r.first.load(), b = r.second.load();
      if (a)
        hazards[count++] = a;
      if (b)
        hazards[count++] = b;
    }
    size_t freed = 0;
    while (list) {
      auto p = list;
      list = p->retired_next;
      bool protected_now = false;
      for (size_t i = 0; i < count; ++i)
        if (hazards[i] == p) {
          protected_now = true;
          break;
        }
      if (protected_now)
        retire(p);
      else {
        {detail::queue_profile_scope<Hooks,detail::node_release> release_profile;delete p;}
        ++freed;
      }
    }
    return freed;
  }
  void clear() {
    auto dummy = new node;
    destroy_quiescent();
    head_.store(dummy);
    tail_.store(dummy);
  }
};
} // namespace ce

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
