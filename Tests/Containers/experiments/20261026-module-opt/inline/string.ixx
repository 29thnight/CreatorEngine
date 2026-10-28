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

export module Creator.Containers:string;
export import :memory;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "memory.h"
// CE_HEADER_INCLUDE #include <string.h>
namespace ce {
class string_view {
  const char *p_ = nullptr;
  size_t n_ = 0;

public:
  static constexpr size_t npos = SIZE_MAX;
  constexpr string_view() noexcept = default;
  constexpr string_view(const char *p, size_t n) noexcept : p_(p), n_(n) {}
  inline string_view(const char *p) noexcept : p_(p), n_(strlen(p)) {}
  constexpr const char *data() const noexcept { return p_; }
  constexpr size_t size() const noexcept { return n_; }
  constexpr bool empty() const noexcept { return !n_; }
  char operator[](size_t i) const noexcept { return p_[i]; }
  string_view substr(size_t pos, size_t count = npos) const {
    if (pos > n_)
      throw bounds_error{};
    auto n = n_ - pos;
    if (count < n)
      n = count;
    return {pos ? p_ + pos : p_, n};
  }
  inline size_t find(string_view v, size_t pos = 0) const noexcept {
    if (pos > n_ || v.n_ > n_ - pos)
      return npos;
    if (!v.n_)
      return pos;
    auto cur = p_ + pos;
    auto stop = p_ + n_ - v.n_ + 1;
    while (cur < stop) {
      auto hit = static_cast<const char *>(
          memchr(cur, static_cast<unsigned char>(v.p_[0]), size_t(stop - cur)));
      if (!hit)
        return npos;
      if (memcmp(hit, v.p_, v.n_) == 0)
        return size_t(hit - p_);
      cur = hit + 1;
    }
    return npos;
  }
  int compare(string_view o) const noexcept {
    auto n = n_ < o.n_ ? n_ : o.n_;
    int c = n ? memcmp(p_, o.p_, n) : 0;
    return c ? c : (n_ < o.n_ ? -1 : n_ > o.n_ ? 1 : 0);
  }
  bool operator==(string_view o) const noexcept {
    return n_ == o.n_ && (!n_ || memcmp(p_, o.p_, n_) == 0);
  }
  bool starts_with(string_view v) const noexcept {
    return v.n_ <= n_ && (!v.n_ || memcmp(p_, v.p_, v.n_) == 0);
  }
};
template <class Allocator = heap_allocator> class basic_string {
  static_assert(detail::nothrow_policy<Allocator>);
  static constexpr size_t short_capacity = 23;
  union storage {
    char inline_bytes[short_capacity + 1];
    char *large;
    storage() noexcept {}
  } data_;
  size_t n_ = 0, cap_ = short_capacity;
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;
  bool is_inline() const noexcept { return cap_ == short_capacity; }
  char *ptr() noexcept {
    return is_inline() ? data_.inline_bytes : data_.large;
  }
  const char *ptr() const noexcept {
    return is_inline() ? data_.inline_bytes : data_.large;
  }
  void release() noexcept {
    if (!is_inline())
      alloc_.deallocate(data_.large, cap_ + 1, alignof(char));
  }
  void initialize(string_view v) {
    if (v.size() <= short_capacity) {
      auto n = v.size();
      // Fixed-width copies avoid a CRT call without reading past the view.
      if (n >= 16) {
        memcpy(data_.inline_bytes, v.data(), 16);
        memcpy(data_.inline_bytes + n - 8, v.data() + n - 8, 8);
      } else if (n >= 8) {
        memcpy(data_.inline_bytes, v.data(), 8);
        memcpy(data_.inline_bytes + n - 8, v.data() + n - 8, 8);
      } else if (n >= 4) {
        memcpy(data_.inline_bytes, v.data(), 4);
        memcpy(data_.inline_bytes + n - 4, v.data() + n - 4, 4);
      } else if (n) {
        data_.inline_bytes[0] = v[0];
        data_.inline_bytes[n / 2] = v[n / 2];
        data_.inline_bytes[n - 1] = v[n - 1];
      }
      data_.inline_bytes[v.size()] = 0;
      n_ = v.size();
    } else {
      if (v.size() == SIZE_MAX)
        throw std::bad_alloc();
      auto p =
          static_cast<char *>(alloc_.allocate(v.size() + 1, alignof(char)));
      memcpy(p, v.data(), v.size());
      p[v.size()] = 0;
      data_.large = p;
      n_ = cap_ = v.size();
    }
  }
  size_t grown(size_t need) const {
    if (need == SIZE_MAX)
      throw std::bad_alloc();
    auto c = cap_ <= (SIZE_MAX - 1) / 2 ? cap_ * 2 : SIZE_MAX - 1;
    return c > need ? c : need;
  }
  CE_NOINLINE void grow_for(size_t n) { reserve(grown(n)); }

public:
  basic_string() noexcept { data_.inline_bytes[0] = 0; }
  explicit basic_string(const Allocator &a) noexcept : alloc_(a) {
    data_.inline_bytes[0] = 0;
  }
  explicit basic_string(string_view v, const Allocator &a = Allocator{})
      : alloc_(a) {
    initialize(v);
  }
  CE_FORCEINLINE basic_string(const basic_string &o) : alloc_(o.alloc_) {
    if (o.is_inline()) {
      if (o.n_ < 16)
        memcpy(data_.inline_bytes, o.data_.inline_bytes, 16);
      else
        memcpy(data_.inline_bytes, o.data_.inline_bytes,
               sizeof(data_.inline_bytes));
      n_ = o.n_;
    } else
      initialize(o.view());
  }
  basic_string(basic_string &&o) noexcept
      : n_(o.n_), cap_(o.cap_), alloc_(o.alloc_) {
    if (o.is_inline())
      memcpy(data_.inline_bytes, o.data_.inline_bytes, o.n_ + 1);
    else
      data_.large = o.data_.large;
    o.n_ = 0;
    o.cap_ = short_capacity;
    o.data_.inline_bytes[0] = 0;
  }
  ~basic_string() { release(); }
  basic_string &operator=(const basic_string &o) {
    if (this != &o) {
      basic_string copy(o);
      *this = static_cast<basic_string &&>(copy);
    }
    return *this;
  }
  basic_string &operator=(basic_string &&o) noexcept {
    if (this != &o) {
      release();
      alloc_ = o.alloc_;
      n_ = o.n_;
      cap_ = o.cap_;
      if (o.is_inline())
        memcpy(data_.inline_bytes, o.data_.inline_bytes, o.n_ + 1);
      else
        data_.large = o.data_.large;
      o.n_ = 0;
      o.cap_ = short_capacity;
      o.data_.inline_bytes[0] = 0;
    }
    return *this;
  }
  void swap(basic_string &o) noexcept {
    if (this == &o)
      return;
    basic_string tmp(static_cast<basic_string &&>(*this));
    *this = static_cast<basic_string &&>(o);
    o = static_cast<basic_string &&>(tmp);
  }
  Allocator get_allocator() const noexcept { return alloc_; }
  void reserve(size_t c) {
    if (c <= cap_)
      return;
    if (c == SIZE_MAX)
      throw std::bad_alloc();
    auto q = static_cast<char *>(alloc_.allocate(c + 1, alignof(char)));
    memcpy(q, ptr(), n_ + 1);
    release();
    data_.large = q;
    cap_ = c;
  }
  void append(string_view v) {
    if (v.size() > SIZE_MAX - 1 - n_)
      throw std::bad_alloc();
    auto total = n_ + v.size();
    auto source = reinterpret_cast<uintptr_t>(v.data()),
         base = reinterpret_cast<uintptr_t>(ptr());
    bool alias = source >= base && source - base <= n_;
    size_t offset = alias ? size_t(source - base) : 0;
    if (total > cap_)
      reserve(grown(total));
    if (v.size())
      memmove(ptr() + n_, alias ? ptr() + offset : v.data(), v.size());
    n_ = total;
    ptr()[n_] = 0;
  }
  void push_back(char c) {
    if (n_ == cap_)
      grow_for(n_ + 1);
    auto p = ptr();
    auto n = n_;
    p[n] = c;
    p[n + 1] = 0;
    n_ = n + 1;
  }
  void pop_back() noexcept { ptr()[--n_] = 0; }
  void resize(size_t n, char fill = 0) {
    if (n == SIZE_MAX)
      throw std::bad_alloc();
    if (n > cap_)
      reserve(grown(n));
    if (n > n_)
      memset(ptr() + n_, static_cast<unsigned char>(fill), n - n_);
    n_ = n;
    ptr()[n_] = 0;
  }
  void assign(string_view v) {
    auto source = reinterpret_cast<uintptr_t>(v.data()),
         base = reinterpret_cast<uintptr_t>(ptr());
    bool alias = source >= base && source - base <= n_;
    if (v.size() > cap_) {
      basic_string tmp(v, alloc_);
      *this = static_cast<basic_string &&>(tmp);
    } else {
      if (v.size()) {
        if (alias)
          memmove(ptr(), v.data(), v.size());
        else
          memcpy(ptr(), v.data(), v.size());
      }
      n_ = v.size();
      ptr()[n_] = 0;
    }
  }
  void insert(size_t pos, string_view v) {
    if (pos > n_)
      throw bounds_error{};
    basic_string copy(v, alloc_);
    if (copy.size() > SIZE_MAX - 1 - n_)
      throw std::bad_alloc();
    auto total = n_ + copy.size();
    if (total > cap_)
      reserve(grown(total));
    memmove(ptr() + pos + copy.size(), ptr() + pos, n_ - pos + 1);
    if (copy.size())
      memcpy(ptr() + pos, copy.data(), copy.size());
    n_ = total;
  }
  void erase(size_t pos, size_t count = SIZE_MAX) {
    if (pos > n_)
      throw bounds_error{};
    auto n = n_ - pos;
    if (count < n)
      n = count;
    memmove(ptr() + pos, ptr() + pos + n, n_ - pos - n + 1);
    n_ -= n;
  }
  void clear() noexcept {
    n_ = 0;
    ptr()[0] = 0;
  }
  const char *c_str() const noexcept { return ptr(); }
  char *data() noexcept { return ptr(); }
  const char *data() const noexcept { return ptr(); }
  size_t size() const noexcept { return n_; }
  size_t capacity() const noexcept { return cap_; }
  bool empty() const noexcept { return !n_; }
  char &operator[](size_t i) noexcept { return ptr()[i]; }
  const char &operator[](size_t i) const noexcept { return ptr()[i]; }
  string_view view() const noexcept { return {ptr(), n_}; }
  operator string_view() const noexcept { return view(); }
  size_t find(string_view v, size_t pos = 0) const noexcept {
    return view().find(v, pos);
  }
  bool operator==(string_view v) const noexcept { return view() == v; }
  bool operator==(const basic_string &v) const noexcept {
    return view() == v.view();
  }
};
using string = basic_string<>;
} // namespace ce
} // export
// CE_MODULE_BODY_END
