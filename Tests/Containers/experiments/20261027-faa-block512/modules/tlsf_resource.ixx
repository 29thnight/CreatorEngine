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

export module Creator.Containers:tlsf_resource;
export import :memory;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "memory.h"
// CE_HEADER_INCLUDE #include <string.h>
#if defined(_MSC_VER)
// CE_HEADER_INCLUDE #include <intrin.h>
#endif
namespace ce {
// Single-threaded, fixed arena, two-level segregated fit. No upstream fallback.
// 64 first-level / 16 second-level classes. Search rounds up one size class;
// fragmentation can reject a request even when aggregate free bytes suffice.
class tlsf_resource final : public memory_resource {
  struct alignas(16) block {
    size_t bytes;
    block *previous, *next, *free_previous, *free_next;
    bool free;
    unsigned char padding[7];
  };
  static_assert(sizeof(block) == 48);
  static constexpr size_t minimum = sizeof(block) + 16;
  block *heads_[64][16]{};
  uint16_t second_[64]{};
  uint64_t first_ = 0;
  size_t active_ = 0, capacity_ = 0;
  static unsigned high(size_t n) noexcept {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanReverse64(&i, n);
    return unsigned(i);
#else
    return 63u - unsigned(__builtin_clzll(n));
#endif
  }
  static unsigned low(uint64_t n) noexcept {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, n);
    return unsigned(i);
#else
    return unsigned(__builtin_ctzll(n));
#endif
  }
  static void mapping(size_t bytes, unsigned &f, unsigned &s) noexcept {
    f = high(bytes);
    s = unsigned((bytes >> (f - 4)) - 16);
  }
  void insert(block *b) noexcept {
    unsigned f, s;
    mapping(b->bytes, f, s);
    b->free = true;
    b->free_previous = nullptr;
    b->free_next = heads_[f][s];
    if (b->free_next)
      b->free_next->free_previous = b;
    heads_[f][s] = b;
    second_[f] |= uint16_t(1u << s);
    first_ |= uint64_t(1) << f;
  }
  void remove(block *b) noexcept {
    unsigned f, s;
    mapping(b->bytes, f, s);
    if (b->free_previous)
      b->free_previous->free_next = b->free_next;
    else
      heads_[f][s] = b->free_next;
    if (b->free_next)
      b->free_next->free_previous = b->free_previous;
    if (!heads_[f][s]) {
      second_[f] &= uint16_t(~(1u << s));
      if (!second_[f])
        first_ &= ~(uint64_t(1) << f);
    }
    b->free = false;
  }

public:
  tlsf_resource(void *storage, size_t bytes) {
    auto base = reinterpret_cast<uintptr_t>(storage);
    if (!storage || base > UINTPTR_MAX - 15 || bytes > UINTPTR_MAX - base)
      throw std::bad_alloc();
    auto aligned = (base + 15) & ~uintptr_t(15);
    auto skip = size_t(aligned - base);
    if (skip > bytes || bytes - skip < minimum)
      throw std::bad_alloc();
    capacity_ = (bytes - skip) & ~size_t(15);
    auto b = ::new (reinterpret_cast<void *>(aligned)) block{};
    b->bytes = capacity_;
    insert(b);
  }
  tlsf_resource(const tlsf_resource &) = delete;
  tlsf_resource &operator=(const tlsf_resource &) = delete;
  void *allocate(size_t bytes, size_t alignment) override {
    if (!alignment || (alignment & (alignment - 1)))
      throw std::bad_alloc();
    if (!bytes)
      return nullptr;
    constexpr auto overhead = sizeof(block) + sizeof(block *) + 15;
    if (alignment - 1 > SIZE_MAX - overhead ||
        bytes > SIZE_MAX - overhead - (alignment - 1))
      throw std::bad_alloc();
    auto needed = (bytes + overhead + alignment - 1) & ~size_t(15);
    auto f = high(needed);
    auto quantum = size_t(1) << (f - 4);
    if (needed > SIZE_MAX - (quantum - 1))
      throw std::bad_alloc();
    auto rounded = (needed + quantum - 1) & ~(quantum - 1);
    unsigned s;
    mapping(rounded, f, s);
    auto candidates = uint16_t(second_[f] & uint16_t(0xffffu << s));
    if (!candidates) {
      auto larger = f == 63 ? 0 : first_ & (~uint64_t(0) << (f + 1));
      if (!larger)
        throw std::bad_alloc();
      f = low(larger);
      candidates = second_[f];
    }
    s = low(candidates);
    auto b = heads_[f][s];
    remove(b);
    auto base = reinterpret_cast<uintptr_t>(b);
    auto address = (base + sizeof(block) + sizeof(block *) + alignment - 1) &
                   ~uintptr_t(alignment - 1);
    auto consumed = (size_t(address - base) + bytes + 15) & ~size_t(15);
    if (b->bytes - consumed >= minimum) {
      auto tail = ::new (reinterpret_cast<void *>(base + consumed)) block{};
      tail->bytes = b->bytes - consumed;
      tail->previous = b;
      tail->next = b->next;
      if (tail->next)
        tail->next->previous = tail;
      b->next = tail;
      b->bytes = consumed;
      insert(tail);
    }
    // A byte copy also supports alignments smaller than pointer alignment.
    auto cookie = reinterpret_cast<unsigned char *>(address) - sizeof(block *);
    memcpy(cookie, &b, sizeof(b));
    ++active_;
    return reinterpret_cast<void *>(address);
  }
  void deallocate(void *p, size_t, size_t) noexcept override {
    if (!p)
      return;
    block *b;
    memcpy(&b, static_cast<unsigned char *>(p) - sizeof(block *), sizeof(b));
    --active_;
    if (b->previous && b->previous->free) {
      auto previous = b->previous;
      remove(previous);
      previous->bytes += b->bytes;
      previous->next = b->next;
      if (b->next)
        b->next->previous = previous;
      b = previous;
    }
    if (b->next && b->next->free) {
      auto next = b->next;
      remove(next);
      b->bytes += next->bytes;
      b->next = next->next;
      if (b->next)
        b->next->previous = b;
    }
    insert(b);
  }
  size_t active_allocations() const noexcept { return active_; }
  size_t capacity() const noexcept { return capacity_; }
};
} // namespace ce
} // export
// CE_MODULE_BODY_END
