#include "benchmark_support.h"
#include <ce/containers.h>
#include <ce/string.h>
#include <string>
#if defined(CE_PROFILE_NO_REALLOC)
struct array_allocator {
  void *allocate(size_t n, size_t a) { return ce::detail::heap_allocate(n, a); }
  void deallocate(void *p, size_t, size_t a) noexcept {
    ce::detail::heap_deallocate(p, a);
  }
};
#else
using array_allocator = ce::heap_allocator;
#endif
CE_NOINLINE static uint64_t consume(const char *p, size_t n) {
  uint64_t h = 0;
  for (size_t i = 0; i < n; ++i)
    h = h * 131 + static_cast<unsigned char>(p[i]);
  return h;
}
int main() {
  constexpr size_t n = 65536;
  std::vector<uint64_t> keys(n);
  for (size_t i = 0; i < n; ++i)
    keys[i] = i * 7919;
  for (bool reserved : {false, true})
    bench(
        reserved ? "array_reserved" : "array_growth", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 16; ++r) {
            ce::dynamic_array<uint64_t, array_allocator> a;
            if (reserved)
              a.reserve(n);
            for (auto k : keys)
              a.push_back(k);
            for (auto k : a)
              sum += k;
          }
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 16; ++r) {
            std::vector<uint64_t> a;
            if (reserved)
              a.reserve(n);
            for (auto k : keys)
              a.push_back(k);
            for (auto k : a)
              sum += k;
          }
          return sum;
        });
  std::string s = "12345678";
  ce::utf8_string c(ce::utf8_string_view(s.data(), s.size()));
  bench(
      "string_copy", 8,
      [&] {
        uint64_t sum = 0;
        for (int i = 0; i < 32; ++i) {
          ce::utf8_string v(c);
          sum += consume(v.data(), v.size());
        }
        return sum;
      },
      [&] {
        uint64_t sum = 0;
        for (int i = 0; i < 32; ++i) {
          std::string v(s);
          sum += consume(v.data(), v.size());
        }
        return sum;
      });
}
