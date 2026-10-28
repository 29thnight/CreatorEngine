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

export module Creator.Containers:concurrent_queue;
export import :detail_atomic;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "detail/atomic.h"
// CE_HEADER_INCLUDE #include <new>
// CE_HEADER_INCLUDE #include <stddef.h>
// CE_HEADER_INCLUDE #include <stdint.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(                                                               \
    disable : 4324) // intentional cache-line and over-aligned value padding
#endif
namespace ce {
// Blocking MPMC FIFO, segmented storage. Deliberately NOT lock-free.
// Operations are serialized; calls on the same queue from T operations must not
// reenter. Destruction requires all producers/consumers to have joined. T must
// have a nonthrowing destructor. Assignment failure in try_pop preserves the
// slot/count, but a throwing move may have modified source/destination.
template <class T, size_t BlockCapacity = 64, size_t CachedBlocks = 4>
class concurrent_queue {
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  static_assert(BlockCapacity > 0 && BlockCapacity <= SIZE_MAX / sizeof(T));
  struct block {
    block *next = nullptr;
    size_t read = 0, write = 0;
    alignas(T) unsigned char bytes[sizeof(T) * BlockCapacity];
    T *at(size_t i) noexcept {
      return std::launder(reinterpret_cast<T *>(bytes + sizeof(T) * i));
    }
    void *raw(size_t i) noexcept { return bytes + sizeof(T) * i; }
  };
  mutable detail::spin_mutex mutex_;
  block *head_ = nullptr, *tail_ = nullptr, *cache_ = nullptr;
  size_t size_ = 0, cached_ = 0;
  block *acquire_block() {
    if (!cache_)
      return new block;
    auto p = cache_;
    cache_ = p->next;
    --cached_;
    p->next = nullptr;
    p->read = p->write = 0;
    return p;
  }
  void recycle(block *p) noexcept {
    if (cached_ < CachedBlocks) {
      p->next = cache_;
      cache_ = p;
      ++cached_;
    } else
      delete p;
  }
  template <class... Args> void append(Args &&...args) {
    detail::lock_guard guard(mutex_);
    if (size_ == SIZE_MAX)
      throw std::bad_alloc();
    if (tail_ && tail_->write < BlockCapacity) {
      ::new (tail_->raw(tail_->write)) T(static_cast<Args &&>(args)...);
      ++tail_->write;
      ++size_;
      return;
    }
    auto fresh = acquire_block();
    try {
      ::new (fresh->raw(0)) T(static_cast<Args &&>(args)...);
    } catch (...) {
      recycle(fresh);
      throw;
    }
    fresh->write = 1;
    if (tail_)
      tail_->next = fresh;
    else
      head_ = fresh;
    tail_ = fresh;
    ++size_;
  }

public:
  concurrent_queue() noexcept = default;
  concurrent_queue(const concurrent_queue &) = delete;
  concurrent_queue &operator=(const concurrent_queue &) = delete;
  ~concurrent_queue() { clear(); }
  void push(const T &x) { append(x); }
  void push(T &&x) { append(static_cast<T &&>(x)); }
  template <class... Args> void emplace(Args &&...args) {
    append(static_cast<Args &&>(args)...);
  }
  bool try_pop(T &out) {
    detail::lock_guard guard(mutex_);
    if (!size_)
      return false;
    auto p = head_;
    out = static_cast<T &&>(
        *p->at(p->read)); // retain membership until assignment succeeds
    p->at(p->read)->~T();
    ++p->read;
    --size_;
    if (p->read == p->write) {
      if (p == tail_) {
        p->read = p->write = 0;
      } // reuse last block without allocating
      else {
        head_ = p->next;
        recycle(p);
      }
    }
    return true;
  }
  bool empty() const noexcept {
    detail::lock_guard guard(mutex_);
    return size_ == 0;
  }
  size_t size() const noexcept {
    detail::lock_guard guard(mutex_);
    return size_;
  }
  // PPL-compatible spelling; synchronized snapshot here, not a termination
  // protocol.
  size_t unsafe_size() const noexcept { return size(); }
  // Serializes with push/pop. Producers may enqueue again immediately
  // afterward.
  void clear() noexcept {
    detail::lock_guard guard(mutex_);
    while (head_) {
      auto p = head_;
      head_ = p->next;
      for (size_t i = p->read; i < p->write; ++i)
        p->at(i)->~T();
      delete p;
    }
    tail_ = nullptr;
    size_ = 0;
    while (cache_) {
      auto p = cache_;
      cache_ = p->next;
      delete p;
    }
    cached_ = 0;
  }
};
// Exactly one producer and one consumer. Fixed inline storage; never allocates.
// All Capacity slots usable. Failed try_push leaves the input untouched.
// 32-bit sequence arithmetic stays valid across wrap as Capacity divides 2^32.
template <class T, uint32_t Capacity> class bounded_spsc_ring {
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0 &&
                Capacity <= (1u << 30));
  static_assert(size_t(Capacity) <= SIZE_MAX / sizeof(T));
  alignas(64) detail::atomic_u32 head_;
  alignas(64) detail::atomic_u32 tail_;
  alignas(T) unsigned char bytes_[sizeof(T) * Capacity];
  T *at(uint32_t i) noexcept {
    return std::launder(
        reinterpret_cast<T *>(bytes_ + sizeof(T) * (i & (Capacity - 1))));
  }
  template <class... Args> bool append(Args &&...args) {
    auto tail = tail_.load();
    auto head = head_.load();
    if (uint32_t(tail - head) == Capacity)
      return false;
    ::new (static_cast<void *>(bytes_ + sizeof(T) * (tail & (Capacity - 1))))
        T(static_cast<Args &&>(args)...);
    tail_.store(tail + 1);
    return true;
  }

public:
  bounded_spsc_ring() noexcept = default;
  bounded_spsc_ring(const bounded_spsc_ring &) = delete;
  bounded_spsc_ring &operator=(const bounded_spsc_ring &) = delete;
  ~bounded_spsc_ring() {
    auto h = head_.load(), t = tail_.load();
    while (h != t) {
      at(h)->~T();
      ++h;
    }
  }
  bool try_push(const T &x) { return append(x); }
  bool try_push(T &&x) { return append(static_cast<T &&>(x)); }
  template <class... Args> bool try_emplace(Args &&...args) {
    return append(static_cast<Args &&>(args)...);
  }
  bool try_pop(T &out) {
    auto h = head_.load();
    if (h == tail_.load())
      return false;
    out = static_cast<T &&>(*at(h));
    at(h)->~T();
    head_.store(h + 1);
    return true;
  }
  // No concurrent clear/reset. Destructor requires both participants joined.
};
} // namespace ce

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // export
// CE_MODULE_BODY_END
