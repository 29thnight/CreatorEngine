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

export module Creator.Containers:containers;
export import :dynamic_array;
export import :flat_hash_map;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "dynamic_array.h"
// CE_HEADER_INCLUDE #include "flat_hash_map.h"
namespace ce {
// Single-threaded, fixed capacity, no allocation after construction.
template <class T, class Allocator = heap_allocator> class bounded_ring {
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  dynamic_array<T, Allocator> storage_;
  size_t head_ = 0, tail_ = 0, n_ = 0, capacity_;

public:
  explicit bounded_ring(size_t n, const Allocator &a = Allocator{})
      : storage_(a), capacity_(n) {
    storage_.reserve(n);
  }
  bounded_ring(const bounded_ring &) = delete;
  bounded_ring &operator=(const bounded_ring &) = delete;
  ~bounded_ring() { clear(); }
  template <class... A> bool emplace(A &&...a) {
    if (n_ == capacity_)
      return false;
    ::new (static_cast<void *>(storage_.data() + tail_))
        T(static_cast<A &&>(a)...);
    if (++tail_ == capacity_)
      tail_ = 0;
    ++n_;
    return true;
  }
  bool push(const T &v) { return emplace(v); }
  bool push(T &&v) { return emplace(static_cast<T &&>(v)); }
  bool pop(T &out) {
    if (!n_)
      return false;
    auto p = storage_.data() + head_;
    out = static_cast<T &&>(*p);
    p->~T();
    if (++head_ == capacity_)
      head_ = 0;
    --n_;
    return true;
  }
  void clear() noexcept {
    while (n_) {
      storage_.data()[head_].~T();
      if (++head_ == capacity_)
        head_ = 0;
      --n_;
    }
    head_ = tail_ = 0;
  }
  size_t size() const noexcept { return n_; }
  bool empty() const noexcept { return !n_; }
  size_t capacity() const noexcept { return capacity_; }
};
} // namespace ce
} // export
// CE_MODULE_BODY_END
