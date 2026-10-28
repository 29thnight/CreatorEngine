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

export module Creator.Containers:slot_map;
export import :dynamic_array;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "dynamic_array.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // intentional payload alignment
#endif
namespace ce {
template <class T, class Allocator = heap_allocator, size_t ChunkSize = 256>
class slot_map {
  static_assert(ChunkSize > 0 && ChunkSize <= UINT32_MAX);
  static_assert(noexcept(static_cast<T *>(nullptr)->~T()));
  static constexpr uint32_t invalid = UINT32_MAX;
  struct slot {
    uint64_t generation = 1;
    uint32_t next = invalid;
    bool live = false;
    alignas(T) unsigned char bytes[sizeof(T)];
    T *value() noexcept { return std::launder(reinterpret_cast<T *>(bytes)); }
    const T *value() const noexcept {
      return std::launder(reinterpret_cast<const T *>(bytes));
    }
  };
  dynamic_array<slot *, Allocator> chunks_;
  uint32_t free_ = invalid;
  size_t size_ = 0;
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;
  slot &raw(uint32_t index) noexcept {
    return chunks_[index / ChunkSize][index % ChunkSize];
  }
  const slot &raw(uint32_t index) const noexcept {
    return chunks_[index / ChunkSize][index % ChunkSize];
  }
  void grow() {
    auto cap = capacity();
    if (cap > UINT32_MAX - ChunkSize)
      throw std::bad_alloc();
    auto bytes = detail::checked_bytes(ChunkSize, sizeof(slot));
    auto p = static_cast<slot *>(alloc_.allocate(bytes, alignof(slot)));
    for (size_t i = 0; i < ChunkSize; ++i)
      ::new (static_cast<void *>(p + i)) slot;
    try {
      chunks_.push_back(p);
    } catch (...) {
      alloc_.deallocate(p, bytes, alignof(slot));
      throw;
    }
    for (size_t i = ChunkSize; i; --i) {
      p[i - 1].next = free_;
      free_ = uint32_t(cap + i - 1);
    }
  }

public:
  struct handle {
    uint32_t index = invalid;
    uint64_t generation = 0;
    const slot_map *owner = nullptr;
    bool operator==(const handle &) const noexcept = default;
  };
  slot_map() = default;
  explicit slot_map(const Allocator &a) : chunks_(a), alloc_(a) {}
  slot_map(const slot_map &) = delete;
  slot_map &operator=(const slot_map &) = delete;
  slot_map(slot_map &&) = delete;
  slot_map &operator=(slot_map &&) = delete;
  ~slot_map() {
    clear();
    for (auto p : chunks_)
      alloc_.deallocate(p, ChunkSize * sizeof(slot), alignof(slot));
  }
  template <class... A> handle emplace(A &&...a) {
    if (free_ == invalid)
      grow();
    auto index = free_;
    auto &s = raw(index);
    ::new (static_cast<void *>(s.bytes)) T(static_cast<A &&>(a)...);
    free_ = s.next;
    s.live = true;
    ++size_;
    return {index, s.generation, this};
  }
  bool contains(handle h) const noexcept {
    return h.owner == this && h.index < capacity() && raw(h.index).live &&
           raw(h.index).generation == h.generation;
  }
  T *find(handle h) noexcept {
    return contains(h) ? raw(h.index).value() : nullptr;
  }
  const T *find(handle h) const noexcept {
    return contains(h) ? raw(h.index).value() : nullptr;
  }
  T &at(handle h) {
    auto p = find(h);
    if (!p)
      throw invalid_handle{};
    return *p;
  }
  const T &at(handle h) const {
    auto p = find(h);
    if (!p)
      throw invalid_handle{};
    return *p;
  }
  bool erase(handle h) noexcept {
    if (!contains(h))
      return false;
    auto &s = raw(h.index);
    s.value()->~T();
    s.live = false;
    --size_;
    if (s.generation == UINT64_MAX) {
      s.next = invalid;
      return true;
    }
    ++s.generation;
    s.next = free_;
    free_ = h.index;
    return true;
  }
  void clear() noexcept {
    free_ = invalid;
    for (size_t i = capacity(); i; --i) {
      auto &s = raw(uint32_t(i - 1));
      if (s.live) {
        s.value()->~T();
        s.live = false;
        if (s.generation != UINT64_MAX)
          ++s.generation;
      }
      if (s.generation != UINT64_MAX) {
        s.next = free_;
        free_ = uint32_t(i - 1);
      }
    }
    size_ = 0;
  }
  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return !size_; }
  size_t capacity() const noexcept { return chunks_.size() * ChunkSize; }
  template <class F> void for_each(F &&f) {
    for (size_t i = 0; i < capacity(); ++i) {
      auto &s = raw(uint32_t(i));
      if (s.live)
        f(*s.value(), handle{uint32_t(i), s.generation, this});
    }
  }
  template <class F> void for_each(F &&f) const {
    for (size_t i = 0; i < capacity(); ++i) {
      auto &s = raw(uint32_t(i));
      if (s.live)
        f(*s.value(), handle{uint32_t(i), s.generation, this});
    }
  }
};
} // namespace ce

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // export
// CE_MODULE_BODY_END
