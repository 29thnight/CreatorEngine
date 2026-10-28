#include "benchmark_support.h"
#include <ce/containers.h>
#include <unordered_map>
struct counts {
  uint64_t hash = 0, equality = 0;
};
struct counted_hash {
  counts *c;
  uint64_t operator()(uint64_t value) const noexcept {
    ++c->hash;
    return ce::hash<uint64_t>{}(value);
  }
};
struct counted_equal {
  counts *c;
  bool operator()(uint64_t a, uint64_t b) const noexcept {
    ++c->equality;
    return a == b;
  }
};
int main() {
  setup_benchmark();
  uint64_t state = 42;
  for (size_t n : {size_t(4096), size_t(65536)}) {
    std::vector<uint64_t> keys;
    keys.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      keys.push_back(state);
    }
    counts count;
    ce::basic_flat_hash_map<uint64_t, uint64_t, counted_hash, counted_equal>
        probe(ce::heap_allocator{}, counted_hash{&count},
              counted_equal{&count});
    ce::flat_hash_map<uint64_t> map;
    std::unordered_map<uint64_t, uint64_t> reference;
    probe.reserve(n);
    map.reserve(n);
    reference.reserve(n);
    for (auto k : keys) {
      probe.try_emplace(k, k);
      map.try_emplace(k, k);
      reference.try_emplace(k, k);
    }
    count = {};
    uint64_t sum = 0;
    for (unsigned r = 0; r < 32; ++r)
      for (auto k : keys)
        sum += probe.contains(k ^ 1);
    std::printf("map_counts,%zu,%llu,%llu,%llu\n", n,
                (unsigned long long)count.hash,
                (unsigned long long)count.equality, (unsigned long long)sum);
    bench(
        "map_miss", n,
        [&] {
          uint64_t result = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              result += map.find(k ^ 1) != nullptr;
          return result;
        },
        [&] {
          uint64_t result = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              result += reference.find(k ^ 1) != reference.end();
          return result;
        });
  }
}
