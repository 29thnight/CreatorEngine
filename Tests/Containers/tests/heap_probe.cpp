#include "benchmark_support.h"
#include <ce/memory.h>
#include <cstring>
#include <new>
extern "C" uint64_t heap_touch(const char *, size_t);
int main() {
  setup_benchmark();
  for (size_t n : {size_t(24), size_t(32), size_t(48), size_t(64)}) {
    std::vector<char> source(n, 'x');
    auto malloc_work = [&] {
      uint64_t sum = 0;
      for (int i = 0; i < 32; ++i) {
        auto p = static_cast<char *>(std::malloc(n));
        check(p != nullptr);
        std::memcpy(p, source.data(), n);
        sum += heap_touch(p, n);
        std::free(p);
      }
      return sum;
    };
    auto new_work = [&] {
      uint64_t sum = 0;
      for (int i = 0; i < 32; ++i) {
        auto p = static_cast<char *>(::operator new(n));
        std::memcpy(p, source.data(), n);
        sum += heap_touch(p, n);
        ::operator delete(p);
      }
      return sum;
    };
    auto heap_work = [&] {
      uint64_t sum = 0;
      for (int i = 0; i < 32; ++i) {
        auto p = static_cast<char *>(HeapAlloc(GetProcessHeap(), 0, n));
        check(p != nullptr);
        std::memcpy(p, source.data(), n);
        sum += heap_touch(p, n);
        check(HeapFree(GetProcessHeap(), 0, p) != 0);
      }
      return sum;
    };
    bench("malloc_vs_new", n, malloc_work, new_work);
    bench("malloc_vs_heap", n, malloc_work, heap_work);
    ULONG mode = 0;
    check(HeapQueryInformation(GetProcessHeap(), HeapCompatibilityInformation,
                               &mode, sizeof(mode), nullptr) != 0);
    std::printf("heap_mode_after,%zu,%lu\n", n, mode);
  }
}
