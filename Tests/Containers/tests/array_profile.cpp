#include "benchmark_support.h"
#include <Psapi.h>
#include <ce/dynamic_array.h>
#pragma comment(lib, "Psapi.lib")
struct stats {
  uint64_t allocations = 0, bytes = 0, copied = 0;
  uint64_t reallocations = 0, pointer_moves = 0, moved_bytes = 0;
  double alloc_ns = 0, free_ns = 0, growth_ns = 0;
};
static double now() {
  return std::chrono::duration<double, std::nano>(
             clock_type::now().time_since_epoch())
      .count();
}
struct traced {
  using byte_relocation_allocator = traced;
  stats *s;
#ifndef CE_PROFILE_NO_REALLOC
  void *reallocate(void *old, size_t old_size, size_t n, size_t a) {
    auto old_address = reinterpret_cast<uintptr_t>(old);
    auto t = now();
    auto p = ce::detail::heap_reallocate(old, old_size, n, a);
    s->alloc_ns += now() - t;
    ++s->allocations;
    ++s->reallocations;
    s->bytes += n;
    if (old_address && reinterpret_cast<uintptr_t>(p) != old_address) {
      ++s->pointer_moves;
      s->moved_bytes += old_size;
    }
    return p;
  }
#endif
  void *allocate(size_t n, size_t a) {
    auto t = now();
    auto p = ce::detail::heap_allocate(n, a);
    s->alloc_ns += now() - t;
    ++s->allocations;
    s->bytes += n;
    return p;
  }
  void deallocate(void *p, size_t, size_t a) noexcept {
    auto t = now();
    ce::detail::heap_deallocate(p, a);
    s->free_ns += now() - t;
  }
};
template <class T> struct stl_traced {
  using value_type = T;
  stats *s;
  explicit stl_traced(stats &v) noexcept : s(&v) {}
  template <class U> stl_traced(const stl_traced<U> &o) noexcept : s(o.s) {}
  T *allocate(size_t n) {
    return static_cast<T *>(traced{s}.allocate(n * sizeof(T), alignof(T)));
  }
  void deallocate(T *p, size_t n) noexcept {
    traced{s}.deallocate(p, n * sizeof(T), alignof(T));
  }
  template <class U> bool operator==(const stl_traced<U> &o) const noexcept {
    return s == o.s;
  }
};
static DWORD faults() {
  PROCESS_MEMORY_COUNTERS c{};
  check(GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) != 0);
  return c.PageFaultCount;
}
template <bool Custom>
static void profile(size_t n, unsigned sample, bool reserve) {
  stats s;
  auto pf = faults();
  double total;
  if constexpr (Custom) {
    total = measure([&] {
      ce::dynamic_array<uint64_t, traced> a(traced{&s});
      if (reserve)
        a.reserve(n);
      for (size_t i = 0; i < n; ++i) {
        if (a.size() == a.capacity()) {
          s.copied += a.size() * sizeof(uint64_t);
          auto t = now();
          a.push_back(i);
          s.growth_ns += now() - t;
        } else
          a.push_back(i);
      }
      uint64_t sum = 0;
      for (auto v : a)
        sum += v;
      return sum;
    });
  } else {
    total = measure([&] {
      std::vector<uint64_t, stl_traced<uint64_t>> a{stl_traced<uint64_t>(s)};
      if (reserve)
        a.reserve(n);
      for (size_t i = 0; i < n; ++i) {
        if (a.size() == a.capacity()) {
          s.copied += a.size() * sizeof(uint64_t);
          auto t = now();
          a.push_back(i);
          s.growth_ns += now() - t;
        } else
          a.push_back(i);
      }
      uint64_t sum = 0;
      for (auto v : a)
        sum += v;
      return sum;
    });
  }
  std::printf("profile,%s,%zu,%u,%d,%.0f,%.0f,%.0f,%.0f,%llu,%llu,%llu,%lu,%."
              "0f,%llu,%llu,%llu,%llu\n",
              Custom ? "custom" : "stl", n, sample, reserve, total, s.growth_ns,
              s.alloc_ns, s.free_ns, (unsigned long long)s.allocations,
              (unsigned long long)s.bytes, (unsigned long long)s.copied,
              static_cast<unsigned long>(faults() - pf), last_diagnostic.cpu_ns,
              (unsigned long long)last_diagnostic.cycles,
              (unsigned long long)s.reallocations,
              (unsigned long long)s.pointer_moves,
              (unsigned long long)s.moved_bytes);
}
int main() {
  setup_benchmark();
  std::puts(
      "kind,impl,n,sample,reserved,total_ns,growth_ns,allocate_ns,free_ns,"
      "allocations,allocated_bytes,relocation_demand_bytes,page_faults,cpu_ns,"
      "cycles,reallocations,pointer_moves,moved_old_capacity_bytes");
  for (size_t n : {size_t(65536), size_t(1048576)})
    for (bool reserve : {false, true})
      for (unsigned i = 0; i < 15; ++i) {
        if (i & 1) {
          profile<false>(n, i, reserve);
          profile<true>(n, i, reserve);
        } else {
          profile<true>(n, i, reserve);
          profile<false>(n, i, reserve);
        }
      }
}
