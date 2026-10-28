#pragma once
#include "memory.h"
#include <string.h>
namespace ce {
template <class T, class Allocator = heap_allocator> class dynamic_array {
  static_assert(detail::nothrow_policy<Allocator>);
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  static_assert(noexcept(Allocator(
      static_cast<const Allocator &>(*static_cast<Allocator *>(nullptr)))));
  T *p_ = nullptr;
  size_t n_ = 0, cap_ = 0;
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;
  static void destroy(T *p, size_t n) noexcept {
    if constexpr (!detail::trivial<T>)
      for (size_t i = n; i; --i)
        p[i - 1].~T();
  }
  T *allocate(size_t n) {
    return static_cast<T *>(
        alloc_.allocate(detail::checked_bytes(n, sizeof(T)), alignof(T)));
  }
  void free(T *p, size_t c) noexcept {
    if (p)
      alloc_.deallocate(p, c * sizeof(T), alignof(T));
  }
  void relocate(T *q, size_t &built) {
    if constexpr (detail::trivial<T>) {
      if (n_)
        memcpy(q, p_, n_ * sizeof(T));
      built = n_;
    } else
      for (; built < n_; ++built) {
        if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
          ::new (static_cast<void *>(q + built))
              T(static_cast<T &&>(p_[built]));
        else
          ::new (static_cast<void *>(q + built)) T(p_[built]);
      }
  }
  size_t growth(size_t need) const {
    if (need > SIZE_MAX / sizeof(T))
      throw std::bad_alloc();
    auto limit = SIZE_MAX / sizeof(T);
    size_t c = cap_ ? cap_ : 8;
    if (c > limit)
      c = limit;
    while (c < need) {
      auto delta = c / 2 + 1;
      if (c > limit - delta) {
        c = limit;
        break;
      }
      c += delta;
    }
    return c;
  }
  template <class... A> T &grow_append(A &&...a) {
    auto c = growth(n_ + 1);
    if constexpr (detail::trivial<T> && detail::nothrow_move<T> &&
                  detail::byte_reallocating_allocator<Allocator>) {
      T pending(
          static_cast<A &&>(a)...); // preserve references into old storage
      auto q = static_cast<T *>(
          alloc_.reallocate(p_, cap_ * sizeof(T),
                            detail::checked_bytes(c, sizeof(T)), alignof(T)));
      p_ = q;
      cap_ = c;
      ::new (static_cast<void *>(p_ + n_)) T(static_cast<T &&>(pending));
      return p_[n_++];
    }
    auto q = allocate(c);
    size_t built = 0;
    bool tail = false;
    try {
      ::new (static_cast<void *>(q + n_)) T(static_cast<A &&>(a)...);
      tail = true;
      relocate(q, built);
    } catch (...) {
      destroy(q, built);
      if (tail)
        q[n_].~T();
      free(q, c);
      throw;
    }
    destroy(p_, n_);
    free(p_, cap_);
    p_ = q;
    cap_ = c;
    ++n_;
    return p_[n_ - 1];
  }

public:
  dynamic_array() noexcept = default;
  explicit dynamic_array(const Allocator &a) noexcept : alloc_(a) {}
  dynamic_array(const dynamic_array &o)
    requires(detail::copyable<T>)
      : alloc_(o.alloc_) {
    if (!o.n_)
      return;
    p_ = allocate(o.n_);
    cap_ = o.n_;
    try {
      if constexpr (detail::trivial<T>) {
        memcpy(p_, o.p_, o.n_ * sizeof(T));
        n_ = o.n_;
      } else
        for (; n_ < o.n_; ++n_)
          ::new (static_cast<void *>(p_ + n_)) T(o.p_[n_]);
    } catch (...) {
      destroy(p_, n_);
      free(p_, cap_);
      throw;
    }
  }
  dynamic_array(dynamic_array &&o) noexcept
      : p_(o.p_), n_(o.n_), cap_(o.cap_), alloc_(o.alloc_) {
    o.p_ = nullptr;
    o.n_ = o.cap_ = 0;
  }
  dynamic_array &operator=(const dynamic_array &o)
    requires(detail::copyable<T>)
  {
    if (this != &o) {
      dynamic_array copy(o);
      swap(copy);
    }
    return *this;
  }
  dynamic_array &operator=(dynamic_array &&o) noexcept {
    if (this != &o) {
      clear();
      free(p_, cap_);
      alloc_ = o.alloc_;
      p_ = o.p_;
      n_ = o.n_;
      cap_ = o.cap_;
      o.p_ = nullptr;
      o.n_ = o.cap_ = 0;
    }
    return *this;
  }
  ~dynamic_array() {
    destroy(p_, n_);
    free(p_, cap_);
  }
  void swap(dynamic_array &o) noexcept {
    auto p = p_;
    p_ = o.p_;
    o.p_ = p;
    auto n = n_;
    n_ = o.n_;
    o.n_ = n;
    auto c = cap_;
    cap_ = o.cap_;
    o.cap_ = c;
    auto a = alloc_;
    alloc_ = o.alloc_;
    o.alloc_ = a;
  }
  Allocator get_allocator() const noexcept { return alloc_; }
  void reserve(size_t c) {
    if (c <= cap_)
      return;
    if constexpr (detail::trivial<T> && detail::nothrow_move<T> &&
                  detail::byte_reallocating_allocator<Allocator>) {
      auto q = static_cast<T *>(
          alloc_.reallocate(p_, cap_ * sizeof(T),
                            detail::checked_bytes(c, sizeof(T)), alignof(T)));
      p_ = q;
      cap_ = c;
      return;
    }
    auto q = allocate(c);
    size_t built = 0;
    try {
      relocate(q, built);
    } catch (...) {
      destroy(q, built);
      free(q, c);
      throw;
    }
    destroy(p_, n_);
    free(p_, cap_);
    p_ = q;
    cap_ = c;
  }
  template <class... A> T &emplace_back(A &&...a) {
    if (n_ == cap_)
      return grow_append(static_cast<A &&>(a)...);
    auto p = ::new (static_cast<void *>(p_ + n_)) T(static_cast<A &&>(a)...);
    ++n_;
    return *p;
  }
  void push_back(const T &v) { emplace_back(v); }
  void push_back(T &&v) { emplace_back(static_cast<T &&>(v)); }
  void pop_back() noexcept { p_[--n_].~T(); }
  void clear() noexcept {
    destroy(p_, n_);
    n_ = 0;
  }
  void resize(size_t n) {
    if (n < n_) {
      destroy(p_ + n, n_ - n);
      n_ = n;
      return;
    }
    if (n > cap_)
      reserve(growth(n));
    auto old = n_;
    try {
      for (; n_ < n; ++n_)
        ::new (static_cast<void *>(p_ + n_)) T();
    } catch (...) {
      destroy(p_ + old, n_ - old);
      n_ = old;
      throw;
    }
  }
  void resize(size_t n, const T &value) {
    if (n <= n_) {
      resize(n);
      return;
    }
    T copy(value);
    if (n > cap_)
      reserve(growth(n));
    auto old = n_;
    try {
      for (; n_ < n; ++n_)
        ::new (static_cast<void *>(p_ + n_)) T(copy);
    } catch (...) {
      destroy(p_ + old, n_ - old);
      n_ = old;
      throw;
    }
  }
  T *erase(size_t pos, size_t count = 1) {
    if (pos > n_ || count > n_ - pos)
      throw bounds_error{};
    if (!count)
      return pos ? p_ + pos : p_;
    if constexpr (detail::trivial<T>) {
      memmove(p_ + pos, p_ + pos + count, (n_ - pos - count) * sizeof(T));
    } else
      for (size_t i = pos; i < n_ - count; ++i)
        p_[i] = static_cast<T &&>(p_[i + count]);
    destroy(p_ + n_ - count, count);
    n_ -= count;
    return p_ + pos;
  }
  template <class... A> T &emplace(size_t pos, A &&...a) {
    if (pos > n_)
      throw bounds_error{};
    if (pos == n_)
      return emplace_back(static_cast<A &&>(a)...);
    T value(static_cast<A &&>(a)...);
    dynamic_array fresh(alloc_);
    fresh.reserve(growth(n_ + 1));
    for (size_t i = 0; i < pos; ++i) {
      if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
        fresh.emplace_back(static_cast<T &&>(p_[i]));
      else
        fresh.emplace_back(p_[i]);
    }
    fresh.emplace_back(static_cast<T &&>(value));
    for (size_t i = pos; i < n_; ++i) {
      if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
        fresh.emplace_back(static_cast<T &&>(p_[i]));
      else
        fresh.emplace_back(p_[i]);
    }
    swap(fresh);
    return p_[pos];
  }
  T &insert(size_t pos, const T &v) { return emplace(pos, v); }
  T &insert(size_t pos, T &&v) { return emplace(pos, static_cast<T &&>(v)); }
  bool empty() const noexcept { return !n_; }
  size_t size() const noexcept { return n_; }
  size_t capacity() const noexcept { return cap_; }
  T *data() noexcept { return p_; }
  const T *data() const noexcept { return p_; }
  T *begin() noexcept { return p_; }
  const T *begin() const noexcept { return p_; }
  T *end() noexcept { return n_ ? p_ + n_ : p_; }
  const T *end() const noexcept { return n_ ? p_ + n_ : p_; }
  T &operator[](size_t i) noexcept { return p_[i]; }
  const T &operator[](size_t i) const noexcept { return p_[i]; }
  T &at(size_t i) {
    if (i >= n_)
      throw bounds_error{};
    return p_[i];
  }
  const T &at(size_t i) const {
    if (i >= n_)
      throw bounds_error{};
    return p_[i];
  }
  T &back() noexcept { return p_[n_ - 1]; }
  const T &back() const noexcept { return p_[n_ - 1]; }
};
} // namespace ce
