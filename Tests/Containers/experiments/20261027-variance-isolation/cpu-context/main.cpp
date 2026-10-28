#include <algorithm>
#include <atomic>
#include <ce/containers.h>
#include <ce/string.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
static void check(bool b) {
  if (!b) {
    std::fprintf(stderr, "CHECK FAILED\n");
    std::abort();
  }
}
static uint64_t rng = 42;
static uint64_t random64() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}
CE_NOINLINE static uint64_t consume_bytes(const char *p, size_t n) {
  uint64_t h = 0;
  for (size_t i = 0; i < n; ++i)
    h = h * 131 + static_cast<unsigned char>(p[i]);
  return h;
}
static void tests() {
  ce::dynamic_array<uint64_t> a;
  std::vector<uint64_t> b;
  for (size_t i = 0; i < 100000; ++i) {
    a.push_back(i);
    b.push_back(i);
  }
  a.push_back(a[0]);
  check(a.size() == 100001 && a[100000] == 0);
  auto c = a;
  check(c.size() == a.size());
  auto *self = &c;
  c = *self;
  auto d = static_cast<decltype(c) &&>(c);
  check(c.size() == 0 && d[99999] == 99999);
  for (size_t i = 0; i < b.size(); ++i)
    check(a[i] == b[i]);
  a.clear();
  check(a.size() == 0 && a.capacity() >= 100001);
  struct alignas(128) aligned {
    uint64_t x;
    unsigned char padding[120];
  };
  ce::dynamic_array<aligned> al;
  al.push_back({42, {}});
  check(uintptr_t(al.data()) % 128 == 0 && al[0].x == 42);
  bool failed = false;
  try {
    a.reserve(SIZE_MAX);
  } catch (const std::bad_alloc &) {
    failed = true;
  }
  check(failed);
  for (size_t cap : {size_t(0), size_t(1), size_t(7), size_t(256)}) {
    ce::bounded_ring<uint64_t> q(cap);
    std::deque<uint64_t> ref;
    uint64_t out = 0;
    for (size_t i = 0; i < 100000; ++i) {
      if (random64() & 1) {
        auto v = random64();
        bool ok = q.push(v);
        check(ok == (ref.size() < cap));
        if (ok)
          ref.push_back(v);
      } else {
        bool ok = q.pop(out);
        check(ok == !ref.empty());
        if (ok) {
          check(out == ref.front());
          ref.pop_front();
        }
      }
      check(q.size() == ref.size());
    }
  }
  ce::flat_hash_map<uint64_t> m;
  std::unordered_map<uint64_t, uint64_t> ref;
  for (size_t i = 0; i < 500000; ++i) {
    auto k = random64() % 8192;
    switch (random64() % 3) {
    case 0: {
      auto v = random64();
      m.insert_or_assign(k, v);
      ref.insert_or_assign(k, v);
      break;
    }
    case 1:
      check(m.erase(k) == (ref.erase(k) != 0));
      break;
    default: {
      auto p = m.find(k);
      auto it = ref.find(k);
      check(bool(p) == (it != ref.end()));
      if (p)
        check(*p == it->second);
    }
    }
    check(m.size() == ref.size());
  }
  m.insert_or_assign(0, 1);
  m.insert_or_assign(UINT64_MAX, 2);
  check(*m.find(0) == 1 && *m.find(UINT64_MAX) == 2);
  for (auto &kv : ref)
    if (kv.first != 0 && kv.first != UINT64_MAX)
      check(*m.find(kv.first) == kv.second);
  ce::utf8_string text;
  std::string expected;
  for (size_t i = 0; i < 10000; ++i) {
    char ch = char(random64() % 256);
    text.push_back(ch);
    expected.push_back(ch);
  }
  check(text.size() == expected.size() &&
        memcmp(text.data(), expected.data(), expected.size()) == 0);
  text.append(text.view());
  expected += expected;
  check(text.view() == ce::utf8_string_view(expected.data(), expected.size()));
  ce::utf8_string short_text(ce::utf8_string_view("hello"));
  short_text.append(short_text.view());
  check(short_text.view() == ce::utf8_string_view("hellohello"));
  auto copied = text;
  auto *self_string = &copied;
  copied = *self_string;
  auto moved = static_cast<ce::utf8_string &&>(copied);
  check(copied.size() == 0 && moved.view() == text.view());
  ce::utf8_string utf(ce::utf8_string_view("한글 UTF-8"));
  check(utf.size() == strlen("한글 UTF-8"));
  check(utf.find(ce::utf8_string_view("UTF")) == 7);
  check(utf.find({}) == 0);
  check(utf.find(ce::utf8_string_view("missing")) == ce::utf8_string_view::npos);
  text.clear();
  check(text.size() == 0 && text.c_str()[0] == 0);
  for (size_t length : {size_t(0), size_t(1), size_t(23), size_t(24),
                        size_t(47), size_t(128)}) {
    std::string base(length, 'a');
    ce::utf8_string x(ce::utf8_string_view(base.data(), base.size()));
    x.append(x.view());
    base += base;
    check(x.view() == ce::utf8_string_view(base.data(), base.size()));
    if (length) {
      auto view = ce::utf8_string_view(x.data() + 1, x.size() - 1);
      std::string suffix(base.data() + 1, base.size() - 1);
      x.append(view);
      base += suffix;
      check(x.view() == ce::utf8_string_view(base.data(), base.size()));
    }
    for (size_t j = 0; j < 10; ++j) {
      std::string needle(j, 'a');
      for (size_t pos = 0; pos <= base.size() + 1; ++pos)
        check(x.find(ce::utf8_string_view(needle.data(), needle.size()), pos) ==
              base.find(needle, pos));
    }
  }
  std::puts("CORRECTNESS_PASS");
}
#define CE_BENCH_HAS_CHECK
#include "benchmark_support.h"
int main(int argc, char **argv) {
  tests();
  if (argc > 1 && std::strcmp(argv[1], "--test") == 0)
    return 0;
  const bool strings_only = argc > 1 && std::strcmp(argv[1], "--strings") == 0;
  const bool layout_probe =
      argc > 1 && std::strcmp(argv[1], "--sample-layout-probe") == 0;
#ifdef _MSC_VER
  std::printf("compiler,%d,stl,%d,iterator_debug,%d\n", _MSC_FULL_VER,
              _MSVC_STL_VERSION, _ITERATOR_DEBUG_LEVEL);
#else
  std::puts("compiler,non-MSVC");
#endif
  std::puts("kind,case,n,sample,custom_ns,stl_ns,custom_over_stl,custom_spread,"
            "stl_spread");
  for (size_t n : {size_t(8), size_t(23), size_t(24), size_t(64), size_t(4096),
                   size_t(65536)}) {
    if (layout_probe && n != 8 && n != 23)
      continue;
    std::string input(n, 'x');
    input[n / 2] = 'y';
    ce::utf8_string custom_input(ce::utf8_string_view(input.data(), input.size()));
    bench(
        "string_copy", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r) {
            ce::utf8_string a(custom_input);
            sum += consume_bytes(a.data(), a.size());
          }
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r) {
            std::string a(input);
            sum += consume_bytes(a.data(), a.size());
          }
          return sum;
        });
    bench(
        "string_view_construct", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r) {
            ce::utf8_string a(ce::utf8_string_view(input.data(), input.size()));
            sum += consume_bytes(a.data(), a.size());
          }
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r) {
            std::string a(input.data(), input.size());
            sum += consume_bytes(a.data(), a.size());
          }
          return sum;
        });
    bench(
        "string_append", n,
        [&] {
          ce::utf8_string a;
          for (size_t i = 0; i < n; ++i)
            a.push_back(input[i]);
          return uint64_t(a.size()) + uint64_t(a.data()[n / 2]);
        },
        [&] {
          std::string a;
          for (size_t i = 0; i < n; ++i)
            a.push_back(input[i]);
          return uint64_t(a.size()) + uint64_t(a[n / 2]);
        });
    ce::utf8_string search(ce::utf8_string_view(input.data(), n));
    bench(
        "string_find", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            sum += search.find(ce::utf8_string_view("yx"));
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            sum += input.find("yx");
          return sum;
        });
    if (strings_only)
      continue;
    std::vector<uint64_t> keys;
    for (size_t i = 0; i < n; ++i)
      keys.push_back(random64());
    for (bool reserved : {false, true})
      bench(
          reserved ? "array_reserved" : "array_growth", n,
          [&] {
            uint64_t sum = 0;
            for (int r = 0; r < 16; ++r) {
              ce::dynamic_array<uint64_t> a;
              if (reserved)
                a.reserve(n);
              for (auto k : keys)
                a.push_back(k);
              for (size_t i = 0; i < n; ++i)
                sum += a[i];
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
    bench(
        "fifo_batch", n,
        [&] {
          uint64_t sum = 0, v = 0;
          ce::bounded_ring<uint64_t> q(n);
          for (int r = 0; r < 32; ++r) {
            for (auto k : keys)
              check(q.push(k));
            while (q.pop(v))
              sum += v;
          }
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          std::deque<uint64_t> q;
          for (int r = 0; r < 32; ++r) {
            for (auto k : keys)
              q.push_back(k);
            while (!q.empty()) {
              sum += q.front();
              q.pop_front();
            }
          }
          return sum;
        });
    bench(
        "map_build", n,
        [&] {
          ce::flat_hash_map<uint64_t> m;
          m.reserve(n);
          for (auto k : keys)
            m.insert_or_assign(k, k);
          return uint64_t(m.size());
        },
        [&] {
          std::unordered_map<uint64_t, uint64_t> m;
          m.reserve(n);
          for (auto k : keys)
            m.insert_or_assign(k, k);
          return uint64_t(m.size());
        });
    ce::flat_hash_map<uint64_t> m;
    std::unordered_map<uint64_t, uint64_t> ref;
    m.reserve(n);
    ref.reserve(n);
    for (auto k : keys) {
      m.insert_or_assign(k, k);
      ref.insert_or_assign(k, k);
    }
    bench(
        "map_hit", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              sum += *m.find(k);
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              sum += ref.find(k)->second;
          return sum;
        });
    bench(
        "map_miss", n,
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              sum += m.find(k ^ 1) != nullptr;
          return sum;
        },
        [&] {
          uint64_t sum = 0;
          for (int r = 0; r < 32; ++r)
            for (auto k : keys)
              sum += ref.find(k ^ 1) != ref.end();
          return sum;
        });
  }
}
