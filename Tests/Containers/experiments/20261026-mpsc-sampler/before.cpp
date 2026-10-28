#include "benchmark_support.h"
#include <ce/bounded_mpsc_queue.h>
#include <concurrent_queue.h>
#include <memory>
#include <thread>
struct payload {
  uint64_t producer = 0, sequence = 0, words[14]{};
};
template <size_t Cap> struct custom_queue {
  ce::bounded_mpsc_queue<payload, Cap> q;
  bool push(payload &&v) { return q.try_push(static_cast<payload &&>(v)); }
  bool pop(payload &v) { return q.try_pop(v); }
};
struct ppl_queue {
  concurrency::concurrent_queue<payload> q;
  bool push(payload &&v) {
    q.push(static_cast<payload &&>(v));
    return true;
  }
  bool pop(payload &v) { return q.try_pop(v); }
};
template <class Queue> double transfer(unsigned producers) {
  constexpr uint64_t each = 262144;
  auto q = std::make_unique<Queue>();
  std::atomic<bool> start = false;
  std::atomic<unsigned> ready = 0;
  std::vector<std::thread> threads;
  for (unsigned p = 0; p < producers; ++p)
    threads.emplace_back([&, p] {
      pin_benchmark_worker(p);
      ++ready;
      while (!start.load())
        std::this_thread::yield();
      for (uint64_t i = 0; i < each; ++i) {
        payload v;
        v.producer = p;
        v.sequence = i;
        for (unsigned w = 0; w < 14; ++w)
          v.words[w] = (i + 1) * (w + 3) + p;
        while (!q->push(static_cast<payload &&>(v)))
          std::this_thread::yield();
      }
    });
  while (ready.load() != producers)
    std::this_thread::yield();
  std::vector<uint64_t> expected(producers, 0);
  uint64_t sum = 0;
  auto before = clock_type::now();
  start = true;
  for (uint64_t i = 0; i < each * producers; ++i) {
    payload v;
    while (!q->pop(v))
      std::this_thread::yield();
    check(v.producer < producers &&
          v.sequence == expected[size_t(v.producer)]++);
    for (unsigned w = 0; w < 14; ++w) {
      check(v.words[w] == (v.sequence + 1) * (w + 3) + v.producer);
      sum += v.words[w];
    }
  }
  for (auto &t : threads)
    t.join();
  auto after = clock_type::now();
  sink = sum;
  return std::chrono::duration<double, std::nano>(after - before).count();
}
template <size_t Cap> void compare(unsigned producers) {
  std::vector<double> a, b;
  a.reserve(15);
  b.reserve(15);
  for (unsigned s = 0; s < 18; ++s) {
    double x, y;
    if (s & 1) {
      y = transfer<ppl_queue>(producers);
      x = transfer<custom_queue<Cap>>(producers);
    } else {
      x = transfer<custom_queue<Cap>>(producers);
      y = transfer<ppl_queue>(producers);
    }
    if (s < 3)
      continue;
    a.push_back(x);
    b.push_back(y);
    std::printf("mpsc_raw,%zu,%u,%u,%.0f,%.0f\n", Cap, producers, s - 3, x, y);
  }
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  std::printf("mpsc_summary,%zu,%u,%.0f,%.0f,%.4f,%.4f,%.4f\n", Cap, producers,
              a[7], b[7], a[7] / b[7], a[13] / a[1], b[13] / b[1]);
}
int main() {
  setup_benchmark();
  prepare_benchmark_workers();
  for (unsigned p : {1u, 4u}) {
    compare<64>(p);
    compare<1024>(p);
  }
}
