#include <algorithm>
#include <atomic>
#if defined(CE_USE_FAA)
#include <ce/faa_lock_free_queue.h>
#define lock_free_queue faa_lock_free_queue
#else
#include <ce/lock_free_queue.h>
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_MSC_VER) && defined(_WIN32)
#include <concurrent_queue.h>
#endif
static void check(bool ok) {
  if (!ok) {
    std::fputs("LOCK_FREE_CHECK_FAILED\n", stderr);
    std::abort();
  }
}
struct item {
  uint32_t producer = 0, index = 0;
};
struct hooks {
  static inline std::atomic<bool> stop_link{false}, stop_head{false},
      entered{false}, resume{false};
  static inline std::atomic<int> nodes{0}, segments{0};
  static inline std::atomic<bool> stop_reserve{false};
  static void after_reserve() noexcept {
    if (stop_reserve.exchange(false))
      park();
  }
  static void segment_created() noexcept { ++segments; }
  static void segment_destroyed() noexcept { --segments; }
  static void park() {
    entered = true;
    while (!resume.load())
      std::this_thread::yield();
  }
  static void after_link() noexcept {
    if (stop_link.exchange(false))
      park();
  }
  static void after_head_protected() noexcept {
    if (stop_head.exchange(false))
      park();
  }
  static void node_created() noexcept { ++nodes; }
  static void node_destroyed() noexcept { --nodes; }
  static void reset() {
    entered = false;
    resume = false;
  }
};
static void await(std::atomic<bool> &flag) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!flag.load()) {
    check(std::chrono::steady_clock::now() < end);
    std::this_thread::yield();
  }
}
static void interrupted_operations() {
  {
    hooks::reset();
    ce::lock_free_queue<int, 8, hooks> q;
    hooks::stop_link = true;
    std::thread stalled([&] { q.emplace(1); });
    await(hooks::entered);
    q.emplace(2);
    int out;
    check(q.try_pop(out) && out == 1);
    check(q.try_pop(out) && out == 2);
    check(!q.try_pop(out));
    q.release_retired();
#if defined(CE_USE_FAA)
    check(hooks::segments <= 2);
#else
    check(hooks::nodes == 2);
#endif
    hooks::resume = true;
    stalled.join();
    q.release_retired();
#if defined(CE_USE_FAA)
    check(hooks::segments == 1);
#else
    check(hooks::nodes == 1);
#endif
  }
  check(hooks::nodes == 0);
  {
    hooks::reset();
    ce::lock_free_queue<int, 8, hooks> q;
    q.emplace(1);
    hooks::stop_head = true;
    std::thread stalled([&] {
      int out;
      check(!q.try_pop(out));
    });
    await(hooks::entered);
    int out;
    check(q.try_pop(out) && out == 1);
    for (int i = 0; i < 10000; ++i) {
      q.emplace(i);
      check(q.try_pop(out) && out == i);
      if (i % 32 == 0)
        q.release_retired();
    }
    q.release_retired();
#if defined(CE_USE_FAA)
    check(hooks::segments <= 2);
#else
    check(hooks::nodes == 2);
#endif
    hooks::resume = true;
    stalled.join();
    q.release_retired();
#if defined(CE_USE_FAA)
    check(hooks::segments == 1);
#else
    check(hooks::nodes == 1);
#endif
  }
  check(hooks::nodes == 0);
  {
    hooks::reset();
    ce::lock_free_queue<int, 1, hooks> q;
    hooks::stop_link = true;
    std::thread stalled([&] { q.emplace(1); });
    await(hooks::entered);
    auto prepared = decltype(q)::prepare(2);
    bool limit = false;
    try {
      q.publish(std::move(prepared));
    } catch (const ce::queue_operation_limit &) {
      limit = true;
    }
    check(limit && bool(prepared));
    hooks::resume = true;
    stalled.join();
    check(q.publish(std::move(prepared)) && !prepared);
    int out;
    check(q.try_pop(out) && out == 1);
    check(q.try_pop(out) && out == 2);
  }
  check(hooks::nodes == 0);
}
#if defined(CE_USE_FAA)
static void reservation_interruption() {
  for (unsigned prefix : {0u, 255u}) {
    hooks::reset();
    ce::lock_free_queue<int, 8, hooks> q;
    for (unsigned i = 0; i < prefix; ++i)
      q.emplace(int(i));
    hooks::stop_reserve = true;
    std::thread paused([&] { q.emplace(777777); });
    await(hooks::entered);
    int out;
    for (unsigned i = 0; i < prefix; ++i)
      check(q.try_pop(out) && out == int(i));
    for (unsigned i = 0; i < 3000; ++i) {
      q.emplace(int(10000 + i));
      check(q.try_pop(out) && out == int(10000 + i));
      if (i % 32 == 0)
        q.reclaim();
    }
    check(!q.try_pop(out));
    hooks::resume = true;
    paused.join();
    check(q.try_pop(out) && out == 777777);
    check(!q.try_pop(out));
    q.reclaim();
  }
  check(hooks::nodes == 0 && hooks::segments == 0);
}
#endif
#if defined(CE_USE_FAA)
static void registration_and_prepared_tests() {
  {
    ce::lock_free_queue<int, 4, hooks> q;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i)
      ts.emplace_back([&, i] {
        q.emplace(i);
        ++ready;
        while (!release.load())
          std::this_thread::yield();
      });
    while (ready.load() != 4)
      std::this_thread::yield();
    bool capped = false;
    int out = 0;
    try {
      q.try_pop(out);
    } catch (const ce::queue_operation_limit &) {
      capped = true;
    }
    check(capped);
    check(q.reclaim() == 0);
    release = true;
    for (auto &t : ts)
      t.join();
    unsigned seen = 0;
    while (q.try_pop(out)) {
      check(out >= 0 && out < 4);
      seen |= 1u << unsigned(out);
    }
    check(seen == 15);
    q.reclaim();
  }
  check(hooks::nodes == 0 && hooks::segments == 0);
  for (int iteration = 0; iteration < 200; ++iteration) {
    ce::lock_free_queue<int, 8, hooks> q;
    q.emplace(iteration);
    int out;
    check(q.try_pop(out) && out == iteration);
    q.reclaim();
  }
  check(hooks::nodes == 0 && hooks::segments == 0);
  {
    ce::lock_free_queue<int, 8, hooks> q;
    for (int i = 0; i < 256; ++i)
      q.emplace(i);
    auto prepared = decltype(q)::prepare(999);
    auto nodes = hooks::nodes.load(), segments = hooks::segments.load();
    check(q.publish(std::move(prepared)));
    check(hooks::nodes == nodes && hooks::segments == segments);
    int out;
    for (int i = 0; i < 256; ++i)
      check(q.try_pop(out) && out == i);
    check(q.try_pop(out) && out == 999);
    q.reclaim();
  }
  check(hooks::nodes == 0 && hooks::segments == 0);
  {
    ce::lock_free_queue<int, 8, hooks> a, b;
    auto token = a.make_token();
    bool rejected = false;
    try {
      b.push(token, 1);
    } catch (const ce::queue_token_mismatch &) {
      rejected = true;
    }
    check(rejected);
    a.push(token, 7);
    int out;
    check(a.try_pop(token, out) && out == 7);
    a.reclaim(token);
  }
  check(hooks::nodes == 0 && hooks::segments == 0);
}
#endif
struct tracked {
  static inline int live = 0;
  static inline bool fail = false;
  int value;
  explicit tracked(int v = 0) : value(v) {
    if (fail)
      throw std::runtime_error("constructor");
    ++live;
  }
  tracked(const tracked &o) : tracked(o.value) {}
  tracked(tracked &&o) noexcept : value(o.value) {
    ++live;
    o.value = -1;
  }
  tracked &operator=(tracked &&o) noexcept {
    value = o.value;
    o.value = -1;
    return *this;
  }
  ~tracked() noexcept { --live; }
};
static void lifetimes() {
  {
    const ce::lock_free_queue<int> q;
    check(q.empty());
  }
  {
    ce::lock_free_queue<std::filesystem::path> q;
    std::filesystem::path input(L"assets/한글.png"), out;
    q.push(input);
    check(q.try_pop(out) && out == input);
    q.reclaim();
  }
  {
    struct alignas(128) aligned_value {
      unsigned char bytes[128]{};
      aligned_value() { check(reinterpret_cast<uintptr_t>(this) % 128 == 0); }
      aligned_value &operator=(aligned_value &&) noexcept = default;
    };
    ce::lock_free_queue<aligned_value> q;
    q.emplace();
    aligned_value out;
    check(q.try_pop(out));
    q.reclaim();
  }
  {
    ce::lock_free_queue<tracked> q;
    tracked out;
    tracked::fail = true;
    try {
      q.emplace(1);
      check(false);
    } catch (const std::runtime_error &) {
    }
    tracked::fail = false;
    check(q.empty());
    q.emplace(1);
    check(q.try_pop(out) && out.value == 1);
    q.reclaim();
    auto p = decltype(q)::prepare(2);
    auto moved = std::move(p);
    check(!p && moved);
    check(!q.publish(std::move(p)));
    check(q.publish(std::move(moved)));
    q.clear();
    check(q.empty());
    q.emplace(3);
  }
  check(tracked::live == 0);
  {
    ce::lock_free_queue<std::unique_ptr<int>> q;
    q.push(std::make_unique<int>(7));
    std::unique_ptr<int> p;
    check(q.try_pop(p) && *p == 7);
    q.reclaim();
  }
}
static void stress(unsigned producers, unsigned consumers) {
  constexpr unsigned count = 30000;
  ce::lock_free_queue<item> q;
  std::vector<std::atomic<unsigned>> seen(producers * count);
  for (auto &s : seen)
    s = 0;
  std::atomic<bool> go{false};
  std::atomic<unsigned> done{0};
  std::vector<std::thread> threads;
  for (unsigned c = 0; c < consumers; ++c)
    threads.emplace_back([&] {
      while (!go.load())
        std::this_thread::yield();
      item v;
      std::vector<unsigned> next(producers);
      unsigned pops = 0;
      for (;;) {
        bool finished = done.load() == producers;
        if (q.try_pop(v)) {
          check(v.producer < producers && v.index < count);
          check(seen[v.producer * count + v.index].fetch_add(1) == 0);
          if (consumers == 1)
            check(v.index == next[v.producer]++);
          if (++pops % 64 == 0)
            q.reclaim();
        } else if (finished)
          break;
        else
          std::this_thread::yield();
      }
    });
  for (unsigned p = 0; p < producers; ++p)
    threads.emplace_back([&, p] {
      while (!go.load())
        std::this_thread::yield();
      for (unsigned i = 0; i < count; ++i)
        q.push(item{p, i});
      ++done;
    });
  go = true;
  for (auto &t : threads)
    t.join();
  for (auto &s : seen)
    check(s == 1);
  check(q.empty());
  q.reclaim();
}
static void owned_mpmc() {
  ce::lock_free_queue<std::unique_ptr<item>> q;
  constexpr unsigned count = 10000;
  std::vector<std::atomic<unsigned>> seen(4 * count);
  for (auto &v : seen)
    v = 0;
  std::atomic<unsigned> done{0};
  std::vector<std::thread> threads;
  for (unsigned c = 0; c < 4; ++c)
    threads.emplace_back([&] {
      std::unique_ptr<item> v;
      unsigned drained = 0;
      for (;;) {
        bool finished = done.load() == 4;
        if (q.try_pop(v)) {
          check(v && v->producer < 4 && v->index < count);
          check(seen[v->producer * count + v->index].fetch_add(1) == 0);
          if (++drained % 32 == 0)
            q.reclaim();
        } else if (finished)
          break;
        else
          std::this_thread::yield();
      }
    });
  for (unsigned p = 0; p < 4; ++p)
    threads.emplace_back([&, p] {
      for (unsigned i = 0; i < count; ++i)
        q.push(std::make_unique<item>(item{p, i}));
      ++done;
    });
  for (auto &t : threads)
    t.join();
  for (auto &v : seen)
    check(v == 1);
  q.reclaim();
}
struct history_entry {
  unsigned begin = 0, end = 0;
  bool push = false, success = false;
  int value = 0;
};
static bool linearize(const std::vector<history_entry> &history, unsigned mask,
                      std::deque<int> model) {
  if (mask == (1u << history.size()) - 1)
    return true;
  for (unsigned i = 0; i < history.size(); ++i) {
    if (mask & (1u << i))
      continue;
    bool available = true;
    for (unsigned j = 0; j < history.size(); ++j)
      if (!(mask & (1u << j)) && history[j].end < history[i].begin) {
        available = false;
        break;
      }
    if (!available)
      continue;
    auto next = model;
    auto op = history[i];
    if (op.push)
      next.push_back(op.value);
    else if (!op.success) {
      if (!next.empty())
        continue;
    } else {
      if (next.empty() || next.front() != op.value)
        continue;
      next.pop_front();
    }
    if (linearize(history, mask | (1u << i), std::move(next)))
      return true;
  }
  return false;
}
static void histories() {
  for (unsigned run = 0; run < 100; ++run) {
    ce::lock_free_queue<int> q;
    std::vector<history_entry> h(12);
    std::atomic<unsigned> stamp{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> ts;
    for (unsigned t = 0; t < 3; ++t)
      ts.emplace_back([&, t] {
        while (!go.load())
          std::this_thread::yield();
        for (unsigned i = 0; i < 4; ++i) {
          auto &op = h[t * 4 + i];
          op.push = ((t + i + run) & 1) == 0;
          op.value = int(t * 4 + i + 1);
          op.begin = ++stamp;
          if ((i + run) % 2)
            std::this_thread::yield();
          if (op.push) {
            q.push(op.value);
            op.success = true;
          } else
            op.success = q.try_pop(op.value);
          op.end = ++stamp;
        }
      });
    go = true;
    for (auto &t : ts)
      t.join();
    check(linearize(h, 0, {}));
    q.reclaim();
  }
}
template <size_t Words> struct payload {
  uint64_t data[Words]{};
};
template <class T> struct maintained_queue {
  ce::lock_free_queue<T> q;
  void push(T &&v) { q.push(std::move(v)); }
  bool try_pop(T &v) {
    bool ok = q.try_pop(v);
    static thread_local unsigned count = 0;
    if (ok && ++count % 64 == 0)
      q.reclaim();
    return ok;
  }
};
template <class Q, class T>
static double transfer(unsigned producers, unsigned consumers) {
  constexpr unsigned count = 16000;
  Q q;
  std::atomic<unsigned> ready{0}, done{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  std::vector<uint64_t> sums(consumers), counts(consumers);
  for (unsigned c = 0; c < consumers; ++c)
    threads.emplace_back([&, c] {
      ++ready;
      while (!go.load())
        std::this_thread::yield();
      T v;
      for (;;) {
        bool finished = done.load() == producers;
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
      ++ready;
      while (!go.load())
        std::this_thread::yield();
      for (unsigned i = 0; i < count; ++i) {
        T v;
        for (auto &w : v.data)
          w = uint64_t(p) * count + i + 1;
        q.push(std::move(v));
      }
      ++done;
    });
  while (ready.load() != producers + consumers)
    std::this_thread::yield();
  auto start = std::chrono::steady_clock::now();
  go = true;
  for (auto &t : threads)
    t.join();
  auto end = std::chrono::steady_clock::now();
  uint64_t sum = 0, n = 0;
  for (auto v : sums)
    sum += v;
  for (auto v : counts)
    n += v;
  auto total = uint64_t(count) * producers;
  check(n == total && sum == total * (total + 1) / 2);
  return std::chrono::duration<double, std::nano>(end - start).count() /
         double(total);
}
#if defined(_MSC_VER) && defined(_WIN32)
template <size_t Words> static void benchmark(unsigned p, unsigned c) {
  using T = payload<Words>;
  auto a = [&] { return transfer<maintained_queue<T>, T>(p, c); };
  auto b = [&] { return transfer<Concurrency::concurrent_queue<T>, T>(p, c); };
  for (int i = 0; i < 2; ++i) {
    a();
    b();
  }
  std::vector<double> x, y;
  for (int i = 0; i < 9; ++i) {
    double ca, cb;
    if (i & 1) {
      cb = b();
      ca = a();
    } else {
      ca = a();
      cb = b();
    }
    x.push_back(ca);
    y.push_back(cb);
    std::printf("raw,queue,%zu,%u,%u,%d,%.2f,%.2f\n", sizeof(T), p, c, i, ca,
                cb);
  }
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  std::printf("summary,queue,%zu,%u,%u,9,%.2f,%.2f,%.4f,%.4f,%.4f\n", sizeof(T),
              p, c, x[4], y[4], x[4] / y[4], x[7] / x[1], y[7] / y[1]);
}
#endif
int main(int argc, char **) {
#if defined(CE_USE_FAA)
  reservation_interruption();
  registration_and_prepared_tests();
#endif
  lifetimes();
  interrupted_operations();
  histories();
  owned_mpmc();
  for (int i = 0; i < 3; ++i) {
    stress(1, 1);
    stress(4, 1);
    stress(4, 4);
  }
  stress(8, 8);
  std::puts("LOCK_FREE_CORRECTNESS_PASS");
  if (argc > 1)
    return 0;
#if defined(_MSC_VER) && defined(_WIN32)
  std::printf("compiler,%d,stl,%d,iterator_debug,%d\n", _MSC_FULL_VER,
              _MSVC_STL_VERSION, _ITERATOR_DEBUG_LEVEL);
  benchmark<1>(1, 1);
  benchmark<16>(1, 1);
  benchmark<1>(4, 1);
  benchmark<16>(4, 1);
  benchmark<1>(4, 4);
  benchmark<16>(4, 4);
#endif
}
