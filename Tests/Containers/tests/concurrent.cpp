#include <algorithm>
#include <atomic>
#include <ce/concurrent_queue.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(_MSC_VER) && defined(_WIN32)
#include <concurrent_queue.h>
#define CE_TEST_PPL 1
#endif
static void check(bool x) {
  if (!x) {
    std::fputs("CHECK_FAILED\n", stderr);
    std::abort();
  }
}
struct tracked {
  static inline int live = 0;
  static inline bool fail_construct = false, fail_assign = false;
  int value = 0;
  explicit tracked(int v = 0) : value(v) {
    if (fail_construct)
      throw std::runtime_error("construct");
    ++live;
  }
  tracked(const tracked &x) : tracked(x.value) {}
  tracked(tracked &&x) : tracked(x.value) { x.value = -1; }
  tracked &operator=(tracked &&x) {
    if (fail_assign)
      throw std::runtime_error("assign");
    value = x.value;
    x.value = -1;
    return *this;
  }
  ~tracked() noexcept { --live; }
};
static void lifetime_tests() {
  {
    ce::concurrent_queue<tracked, 2, 1> q;
    tracked out;
    tracked::fail_construct = true;
    try {
      q.emplace(1);
      check(false);
    } catch (const std::runtime_error &) {
    }
    tracked::fail_construct = false;
    check(q.empty());
    q.emplace(1);
    q.emplace(2); // force fresh block failure after a full block
    tracked::fail_construct = true;
    try {
      q.emplace(3);
      check(false);
    } catch (const std::runtime_error &) {
    }
    tracked::fail_construct = false;
    check(q.size() == 2);
    tracked::fail_assign = true;
    try {
      q.try_pop(out);
      check(false);
    } catch (const std::runtime_error &) {
    }
    tracked::fail_assign = false;
    check(q.size() == 2);
    check(q.try_pop(out) && out.value == 1);
    check(q.try_pop(out) && out.value == 2);
    check(!q.try_pop(out));
    for (int i = 0; i < 1000; ++i)
      q.emplace(i);
    for (int i = 0; i < 999; ++i) {
      check(q.try_pop(out) && out.value == i);
    }
    q.clear();
    check(q.empty());
    q.emplace(17);
    check(q.try_pop(out) && out.value == 17);
  }
  check(tracked::live == 0);
  {
    ce::concurrent_queue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(7));
    std::unique_ptr<int> out;
    check(q.try_pop(out) && *out == 7);
  }
  {
    ce::concurrent_queue<std::string> q;
    std::string s = "path/한글.png";
    q.push(s);
    std::string out;
    check(q.try_pop(out) && out == s);
  }
  {
    struct alignas(128) value {
      unsigned char bytes[128]{};
    };
    ce::concurrent_queue<value, 2> q;
    value x;
    q.push(x);
    check(q.try_pop(x));
  }
  {
    ce::bounded_spsc_ring<tracked, 2> q;
    tracked out;
    q.try_emplace(1);
    q.try_emplace(2);
    check(!q.try_emplace(3));
    check(q.try_pop(out) && out.value == 1);
    check(q.try_emplace(3));
    check(q.try_pop(out) && out.value == 2);
    check(q.try_pop(out) && out.value == 3);
    check(!q.try_pop(out));
    q.try_emplace(4);
  }
  check(tracked::live == 0);
  {
    ce::bounded_spsc_ring<std::unique_ptr<int>, 1> q;
    auto a = std::make_unique<int>(1);
    check(q.try_push(std::move(a)) && !a);
    auto b = std::make_unique<int>(2);
    check(!q.try_push(std::move(b)) && b);
    std::unique_ptr<int> out;
    check(q.try_pop(out) && *out == 1);
  }
}
struct item {
  uint32_t producer = 0, index = 0;
};
template <class Queue>
static void stress(unsigned producers, unsigned consumers) {
  constexpr unsigned count = 30000;
  Queue q;
  std::vector<std::atomic<unsigned>> seen(producers * count);
  for (auto &s : seen)
    s.store(0);
  std::atomic<unsigned> done{0};
  std::atomic<bool> go{false};
  std::atomic<uint64_t> consumed{0};
  std::vector<std::thread> threads;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (unsigned c = 0; c < consumers; ++c)
    threads.emplace_back([&] {
      std::vector<unsigned> next(producers, 0);
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      item v;
      for (;;) {
        auto finished = done.load(std::memory_order_acquire) == producers;
        if (q.try_pop(v)) {
          check(v.producer < producers && v.index < count);
          check(seen[v.producer * count + v.index].fetch_add(1) == 0);
          if (consumers == 1)
            check(v.index == next[v.producer]++);
          consumed.fetch_add(1);
        } else if (finished)
          break;
        else {
          check(std::chrono::steady_clock::now() < deadline);
          std::this_thread::yield();
        }
      }
    });
  for (unsigned p = 0; p < producers; ++p)
    threads.emplace_back([&, p] {
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      for (unsigned i = 0; i < count; ++i)
        q.push(item{p, i});
      done.fetch_add(1, std::memory_order_release);
    });
  go.store(true, std::memory_order_release);
  for (auto &t : threads)
    t.join();
  check(consumed == uint64_t(producers) * count);
  for (auto &s : seen)
    check(s == 1);
  check(q.empty());
}
static void owned_mpsc_test() {
  ce::concurrent_queue<std::unique_ptr<item>, 16, 2> q;
  std::atomic<unsigned> done{0};
  std::vector<std::thread> threads;
  for (unsigned p = 0; p < 4; ++p)
    threads.emplace_back([&, p] {
      for (unsigned i = 0; i < 20000; ++i)
        q.push(std::make_unique<item>(item{p, i}));
      done.fetch_add(1, std::memory_order_release);
    });
  unsigned next[4]{};
  std::unique_ptr<item> v;
  unsigned total = 0;
  for (;;) {
    bool finished = done.load(std::memory_order_acquire) == 4;
    if (q.try_pop(v)) {
      check(v && v->producer < 4 && v->index == next[v->producer]++);
      ++total;
    } else if (finished)
      break;
    else
      std::this_thread::yield();
  }
  for (auto &t : threads)
    t.join();
  check(total == 80000 && q.empty());
}
static void spsc_test() {
  auto q =
      std::make_unique<ce::bounded_spsc_ring<std::unique_ptr<uint64_t>, 64>>();
  constexpr uint64_t count = 200000;
  std::thread producer([&] {
    for (uint64_t i = 0; i < count; ++i) {
      auto p = std::make_unique<uint64_t>(i);
      while (!q->try_push(std::move(p)))
        std::this_thread::yield();
    }
  });
  std::unique_ptr<uint64_t> p;
  for (uint64_t i = 0; i < count; ++i) {
    while (!q->try_pop(p))
      std::this_thread::yield();
    check(*p == i);
  }
  producer.join();
  check(!q->try_pop(p));
}
template <size_t Words> struct payload {
  uint64_t data[Words]{};
};
using Clock = std::chrono::steady_clock;
template <class Queue, class T>
double transfer(unsigned producers, unsigned consumers, unsigned count) {
  Queue q;
  std::atomic<unsigned> ready{0}, done{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  std::vector<uint64_t> sums(consumers), counts(consumers);
  for (unsigned c = 0; c < consumers; ++c)
    threads.emplace_back([&, c] {
      ready.fetch_add(1);
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      T v;
      for (;;) {
        bool finished = done.load(std::memory_order_acquire) == producers;
        if (q.try_pop(v)) {
          sums[c] += v.data[0];
          ++counts[c];
        } else if (finished)
          break;
        else
          std::this_thread::yield();
      }
    });
  for (unsigned p = 0; p < producers; ++p)
    threads.emplace_back([&, p] {
      ready.fetch_add(1);
      while (!go.load(std::memory_order_acquire))
        std::this_thread::yield();
      for (unsigned i = 0; i < count; ++i) {
        T v;
        for (auto &word : v.data)
          word = uint64_t(p) * count + i + 1;
        q.push(std::move(v));
      }
      done.fetch_add(1, std::memory_order_release);
    });
  while (ready.load() != producers + consumers)
    std::this_thread::yield();
  auto start = Clock::now();
  go.store(true, std::memory_order_release);
  for (auto &t : threads)
    t.join();
  auto stop = Clock::now();
  uint64_t sum = 0, total = 0;
  for (auto s : sums)
    sum += s;
  for (auto c : counts)
    total += c;
  auto n = uint64_t(producers) * count;
  check(total == n && sum == n * (n + 1) / 2);
  return std::chrono::duration<double, std::nano>(stop - start).count() /
         double(n);
}
template <class T> struct spsc_adapter {
  ce::bounded_spsc_ring<T, 1024> q;
  void push(T &&v) {
    while (!q.try_push(std::move(v)))
      std::this_thread::yield();
  }
  bool try_pop(T &v) { return q.try_pop(v); }
};
#if CE_TEST_PPL
template <size_t Words>
static void benchmark(unsigned p, unsigned c, bool spsc) {
  using T = payload<Words>;
  auto a = [&] {
    return spsc ? transfer<spsc_adapter<T>, T>(p, c, 16000)
                : transfer<ce::concurrent_queue<T>, T>(p, c, 16000);
  };
  auto b = [&] {
    return transfer<Concurrency::concurrent_queue<T>, T>(p, c, 16000);
  };
  for (int w = 0; w < 2; ++w) {
    a();
    b();
  }
  std::vector<double> x, y;
  for (int r = 0; r < 9; ++r) {
    double ca, pa;
    if (r & 1) {
      pa = b();
      ca = a();
    } else {
      ca = a();
      pa = b();
    }
    x.push_back(ca);
    y.push_back(pa);
    std::printf("raw,%s,%zu,%u,%u,%d,%.2f,%.2f\n", spsc ? "spsc" : "mpmc",
                sizeof(T), p, c, r, ca, pa);
  }
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  std::printf("summary,%s,%zu,%u,%u,9,%.2f,%.2f,%.4f,%.4f,%.4f\n",
              spsc ? "spsc" : "mpmc", sizeof(T), p, c, x[4], y[4], x[4] / y[4],
              x[7] / x[1], y[7] / y[1]);
}
#endif
int main(int argc, char **) {
  lifetime_tests();
  owned_mpsc_test();
  for (int i = 0; i < 3; ++i) {
    stress<ce::concurrent_queue<item>>(1, 1);
    stress<ce::concurrent_queue<item>>(4, 1);
    stress<ce::concurrent_queue<item>>(4, 4);
  }
  spsc_test();
  std::puts("CONCURRENT_CORRECTNESS_PASS");
  if (argc > 1)
    return 0;
#if CE_TEST_PPL
  std::printf("compiler,%d,stl,%d,iterator_debug,%d\n", _MSC_FULL_VER,
              _MSVC_STL_VERSION, _ITERATOR_DEBUG_LEVEL);
  std::puts("kind,type,bytes,producers,consumers,sample,custom_ns_per_item,ppl_"
            "ns_per_item,custom_over_ppl,custom_spread,ppl_spread");
  benchmark<1>(1, 1, true);
  benchmark<16>(1, 1, true);
  benchmark<1>(1, 1, false);
  benchmark<16>(1, 1, false);
  benchmark<1>(4, 1, false);
  benchmark<16>(4, 1, false);
  benchmark<1>(4, 4, false);
  benchmark<16>(4, 4, false);
#endif
}
