#include "benchmark_support.h"
#include <ce/age_buckets.h>
#include <ce/containers.h>
#include <ce/inplace_function.h>
#include <ce/intrusive_lru.h>
#include <ce/radix_sort.h>
#include <ce/sequence_buffer.h>
#include <ce/slot_map.h>
#include <ce/string_atom.h>
#include <ce/tlsf_resource.h>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
static uint64_t state = 923;
static uint64_t random64() {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}
int main() {
#ifdef _MSC_VER
  std::printf("compiler,%d,stl,%d,iterator_debug,%d\n", _MSC_FULL_VER,
              _MSVC_STL_VERSION, _ITERATOR_DEBUG_LEVEL);
#endif
  std::puts("kind,case,n,sample,custom_ns,stl_ns,custom_over_stl,custom_spread,"
            "stl_spread");
  for (size_t n : {size_t(256), size_t(4096)}) {
    std::vector<std::string> keys;
    for (size_t i = 0; i < n; ++i)
      keys.push_back("asset/path/" + std::to_string(random64()));
    bench(
        "string_map_build", n,
        [&] {
          ce::string_hash_map<uint64_t> map;
          map.reserve(n);
          for (size_t i = 0; i < n; ++i)
            map.try_emplace(
                ce::string(ce::string_view(keys[i].data(), keys[i].size())), i);
          uint64_t sum = 0;
          for (auto &e : map)
            sum += e.value();
          return sum;
        },
        [&] {
          std::unordered_map<std::string, uint64_t> map;
          map.reserve(n);
          for (size_t i = 0; i < n; ++i)
            map.try_emplace(keys[i], i);
          uint64_t sum = 0;
          for (auto &e : map)
            sum += e.second;
          return sum;
        });
    ce::string_hash_map<uint64_t> cm;
    std::unordered_map<std::string, uint64_t> sm;
    for (size_t i = 0; i < n; ++i) {
      cm.try_emplace(
          ce::string(ce::string_view(keys[i].data(), keys[i].size())), i);
      sm.try_emplace(keys[i], i);
    }
    bench(
        "string_map_hit", n,
        [&] {
          uint64_t sum = 0;
          for (auto &k : keys)
            sum += *cm.find(ce::string_view(k.data(), k.size()));
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (auto &k : keys)
            sum += sm.find(k)->second;
          return sum;
        });
    bench(
        "owned_array", n,
        [&] {
          ce::dynamic_array<ce::string> a;
          for (auto &k : keys)
            a.emplace_back(ce::string_view(k.data(), k.size()));
          uint64_t sum = 0;
          for (auto &k : a)
            sum += k.size();
          return sum;
        },
        [&] {
          std::vector<std::string> a;
          for (auto &k : keys)
            a.emplace_back(k);
          uint64_t sum = 0;
          for (auto &k : a)
            sum += k.size();
          return sum;
        });
    bench(
        "slot_lifetime", n,
        [&] {
          ce::slot_map<uint64_t> a;
          std::vector<ce::slot_map<uint64_t>::handle> handles;
          handles.reserve(n);
          for (size_t i = 0; i < n; ++i)
            handles.push_back(a.emplace(i));
          uint64_t sum = 0;
          for (auto h : handles) {
            sum += a.at(h);
            a.erase(h);
          }
          return sum;
        },
        [&] {
          std::unordered_map<uint64_t, uint64_t> a;
          std::vector<uint64_t> handles;
          handles.reserve(n);
          for (size_t i = 0; i < n; ++i) {
            a.emplace(i, i);
            handles.push_back(i);
          }
          uint64_t sum = 0;
          for (auto h : handles) {
            sum += a.at(h);
            a.erase(h);
          }
          return sum;
        });
    bench(
        "sequence_window", n,
        [&] {
          ce::sequence_buffer<uint64_t, 256> a;
          uint64_t sum = 0;
          for (uint64_t i = 0; i < n; ++i) {
            a.emplace(i, i);
            sum += *a.find(i);
          }
          return sum + a.size();
        },
        [&] {
          std::map<uint64_t, uint64_t> a;
          uint64_t sum = 0;
          for (uint64_t i = 0; i < n; ++i) {
            a.emplace(i, i);
            if (i >= 256)
              a.erase(i - 256);
            sum += a.find(i)->second;
          }
          return sum + a.size();
        });
    bench(
        "lru_touch", n,
        [&] {
          ce::dynamic_array<ce::lru_link<>> links;
          links.resize(n);
          ce::intrusive_lru<> a(links.data(), n);
          for (uint32_t i = 0; i < n; ++i)
            a.push_front(i);
          for (uint32_t r = 0; r < 8; ++r)
            for (uint32_t i = 0; i < n; ++i)
              a.touch(i);
          uint64_t sum = 0;
          while (!a.empty())
            sum += a.pop_back();
          return sum;
        },
        [&] {
          std::list<uint32_t> a;
          std::vector<std::list<uint32_t>::iterator> iterators;
          for (uint32_t i = 0; i < n; ++i) {
            a.push_front(i);
            iterators.push_back(a.begin());
          }
          for (uint32_t r = 0; r < 8; ++r)
            for (uint32_t i = 0; i < n; ++i)
              a.splice(a.begin(), a, iterators[i]);
          uint64_t sum = 0;
          while (!a.empty()) {
            sum += a.back();
            a.pop_back();
          }
          return sum;
        });
    ce::string_atom_table atoms;
    std::unordered_set<std::string> strings;
    for (auto &k : keys) {
      atoms.intern({k.data(), k.size()});
      strings.insert(k);
    }
    bench(
        "atom_duplicate", n,
        [&] {
          uint64_t sum = 0;
          for (auto &k : keys)
            sum += atoms.at(atoms.intern({k.data(), k.size()})).size();
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (auto &k : keys)
            sum += strings.insert(k).first->size();
          return sum;
        });
    std::vector<ce::inplace_function<uint64_t(uint64_t)>> cf;
    std::vector<std::function<uint64_t(uint64_t)>> sf;
    for (uint64_t i = 0; i < n; ++i) {
      cf.emplace_back([i](uint64_t x) { return i + x; });
      sf.emplace_back([i](uint64_t x) { return i + x; });
    }
    bench(
        "function_dispatch", n,
        [&] {
          uint64_t sum = 0;
          for (size_t i = 0; i < n; ++i)
            sum += cf[i](i);
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (size_t i = 0; i < n; ++i)
            sum += sf[i](i);
          return sum;
        });
    std::vector<uint64_t> values;
    for (size_t i = 0; i < n; ++i)
      values.push_back(random64());
    bench(
        "radix_sort_u64", n,
        [&] {
          auto a = values;
          ce::radix_sort(a.data(), a.size(),
                         [](uint64_t x) noexcept { return x; });
          uint64_t sum = 0;
          for (auto v : a)
            sum = sum * 131 + v;
          return sum;
        },
        [&] {
          auto a = values;
          std::stable_sort(a.begin(), a.end());
          uint64_t sum = 0;
          for (auto v : a)
            sum = sum * 131 + v;
          return sum;
        });
    bench(
        "age_expiration", n,
        [&] {
          ce::age_buckets<uint64_t, 256> a;
          uint64_t sum = 0;
          for (uint64_t i = 0; i < n; ++i) {
            check(a.emplace(i + 127, i));
            a.expire_through(i,
                             [&](uint64_t &v, uint64_t) noexcept { sum += v; });
          }
          a.expire_through(UINT64_MAX,
                           [&](uint64_t &v, uint64_t) noexcept { sum += v; });
          return sum;
        },
        [&] {
          std::multimap<uint64_t, uint64_t> a;
          uint64_t sum = 0;
          for (uint64_t i = 0; i < n; ++i) {
            a.emplace(i + 127, i);
            while (!a.empty() && a.begin()->first <= i) {
              sum += a.begin()->second;
              a.erase(a.begin());
            }
          }
          for (auto e : a)
            sum += e.second;
          return sum;
        });
    std::vector<unsigned char> arena(2 * 1024 * 1024);
    ce::tlsf_resource tlsf(arena.data(), arena.size());
    bench(
        "fixed_arena_churn", n,
        [&] {
          uint64_t sum = 0;
          for (size_t i = 0; i < n; ++i) {
            auto p = static_cast<uint64_t *>(tlsf.allocate(128, 16));
            *p = i;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            sum += *p;
            tlsf.deallocate(p, 128, 16);
          }
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (size_t i = 0; i < n; ++i) {
            auto p = static_cast<uint64_t *>(::operator new(128));
            *p = i;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            sum += *p;
            ::operator delete(p);
          }
          return sum;
        });
  }
}
