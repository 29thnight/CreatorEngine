#include <atomic>
#if defined(CE_USE_FAA)
#include <ce/faa_lock_free_queue.h>
#define lock_free_queue faa_lock_free_queue
#else
#include <ce/lock_free_queue.h>
#endif
#include <algorithm>
#include <chrono>
#include <concurrent_queue.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>
static void check(bool b) {
  if (!b)
    std::abort();
}
template <size_t Words> struct payload {
  uint64_t x[Words]{};
};
template <class T> struct maintained {
  ce::lock_free_queue<T> q;
#if defined(CE_USE_TOKENS)
  struct participant {
    maintained &owner;
    typename ce::lock_free_queue<T>::access_token token;
    unsigned count = 0;
    explicit participant(maintained &o) : owner(o), token(o.q.make_token()) {}
    void push(T &&v) { owner.q.push(token, static_cast<T &&>(v)); }
    bool try_pop(T &v) {
      bool ok = owner.q.try_pop(token, v);
      if (ok && ++count % 64 == 0)
        owner.q.reclaim(token);
      return ok;
    }
  };
  participant bind() { return participant(*this); }
#endif
  void push(T &&v) { q.push(static_cast<T &&>(v)); }
  bool try_pop(T &v) {
    bool ok = q.try_pop(v);
    static thread_local unsigned n = 0;
    if (ok && ++n % 64 == 0)
      q.reclaim();
    return ok;
  }
};
template <class Q, class T> double run(unsigned p, unsigned c) {
  constexpr unsigned count = 65536;
  auto storage = std::make_unique<Q>();
  auto &q = *storage;
  std::atomic<bool> go{false};
  std::atomic<unsigned> ready{0}, done{0};
  std::vector<std::thread> ts;
  std::vector<uint64_t> sums(c), counts(c);
  for (unsigned i = 0; i < c; ++i)
    ts.emplace_back([&, i] {
#if defined(CE_USE_TOKENS)
      decltype(auto) bound = [&]() -> decltype(auto) {
        if constexpr (requires { q.bind(); })
          return q.bind();
        else
          return (q);
      }();
#else
      auto &bound = q;
#endif
      ++ready;
      while (!go.load())
        std::this_thread::yield();
      T v;
      for (;;) {
        bool end = done.load() == p;
        if (bound.try_pop(v)) {
          for (auto word : v.x)
            sums[i] += word;
          ++counts[i];
        } else if (end)
          break;
        else
          std::this_thread::yield();
      }
    });
  for (unsigned i = 0; i < p; ++i)
    ts.emplace_back([&, i] {
#if defined(CE_USE_TOKENS)
      decltype(auto) bound = [&]() -> decltype(auto) {
        if constexpr (requires { q.bind(); })
          return q.bind();
        else
          return (q);
      }();
#else
      auto &bound = q;
#endif
      ++ready;
      while (!go.load())
        std::this_thread::yield();
      for (unsigned n = 0; n < count; ++n) {
        T v;
        for (auto &w : v.x)
          w = uint64_t(i) * count + n + 1;
        bound.push(static_cast<T &&>(v));
      }
      ++done;
    });
  while (ready.load() != p + c)
    std::this_thread::yield();
  auto start = std::chrono::steady_clock::now();
  go = true;
  for (auto &t : ts)
    t.join();
  auto end = std::chrono::steady_clock::now();
  uint64_t sum = 0, n = 0;
  for (auto v : sums)
    sum += v;
  for (auto v : counts)
    n += v;
  auto expected = uint64_t(p) * count;
  check(n == expected &&
        sum == expected * (expected + 1) / 2 * (sizeof(T) / 8));
  double transfer_ns =
      std::chrono::duration<double, std::nano>(end - start).count() /
      double(expected);
  auto destroy_start = std::chrono::steady_clock::now();
  storage.reset();
  auto destroy_end = std::chrono::steady_clock::now();
  double teardown_ns =
      std::chrono::duration<double, std::nano>(destroy_end - destroy_start)
          .count() /
      double(expected);
  constexpr bool custom = requires(Q obj) { obj.q; };
  std::printf("lifecycle,%s,%zu,%u,%u,%.3f,%.3f,%.3f\n",
              custom ? "custom" : "ppl", sizeof(T), p, c, transfer_ns,
              teardown_ns, transfer_ns + teardown_ns);
  return transfer_ns;
}
template <size_t Words> void pair(unsigned p, unsigned c) {
  using T = payload<Words>;
  auto a = [&] { return run<maintained<T>, T>(p, c); };
  auto b = [&] { return run<Concurrency::concurrent_queue<T>, T>(p, c); };
  for (int i = 0; i < 2; ++i) {
    a();
    b();
  }
  std::vector<double> x, y;
  for (int i = 0; i < 15; ++i) {
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
    std::printf("raw,%zu,%u,%u,%d,%.3f,%.3f\n", sizeof(T), p, c, i, ca, cb);
  }
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  std::printf("summary,%zu,%u,%u,15,%.3f,%.3f,%.5f,%.5f,%.5f\n", sizeof(T), p,
              c, x[7], y[7], x[7] / y[7], x[13] / x[1], y[13] / y[1]);
}
int main() {
  std::printf("compiler,%d,stl,%d,iterator_debug,%d,count_per_producer,65536,"
              "samples,15\n",
              _MSC_FULL_VER, _MSVC_STL_VERSION, _ITERATOR_DEBUG_LEVEL);
  pair<1>(1, 1);
  pair<16>(1, 1);
  pair<1>(4, 1);
  pair<16>(4, 1);
  pair<1>(4, 4);
  pair<16>(4, 4);
}
