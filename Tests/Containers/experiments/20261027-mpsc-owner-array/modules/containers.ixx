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
// Keep erase algorithms independent of container names. Associative containers
// erase nodes; contiguous sequences compact once and erase one suffix.
template<class C,class Predicate> requires requires(C& c,typename C::iterator it) {
  typename C::key_type;c.erase(it);
}
size_t erase_if(C& values,Predicate predicate) {
  const auto before=values.size();
  for(auto it=values.begin();it!=values.end();) {
    if(predicate(*it))it=values.erase(it);else ++it;
  }
  return before-values.size();
}
template<class C,class Predicate> requires (!requires{typename C::key_type;}) && requires(C& c) {
  c.erase(c.begin(),c.end());
}
size_t erase_if(C& values,Predicate predicate) {
  auto output=values.begin();
  for(auto it=values.begin();it!=values.end();++it)if(!predicate(*it)) {
    if(output!=it)*output=static_cast<typename C::value_type&&>(*it);
    ++output;
  }
  const auto before=values.size();values.erase(output,values.end());
  return before-values.size();
}
template<class C,class V> requires (!requires{typename C::key_type;}) && requires(C& c) {c.erase(c.begin(),c.end());}
size_t erase(C& values,const V& value) {
  return erase_if(values,[&](const auto& item){return item==value;});
}
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
