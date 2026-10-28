#include "benchmark_support.h"
#include <ce/bounded_mpsc_queue.h>
#include <concurrent_queue.h>
#include <memory>
#include <thread>
struct payload {
  uint64_t producer = 0, sequence = 0, words[14]{};
};
static_assert(sizeof(payload) == 128);
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
static bool polling_pause = false;
static bool startup_pause = false;
static unsigned batch_repeats = 1;
static void wait_policy(bool pause) noexcept {
  if (pause) {
    for (unsigned i = 0; i < 32; ++i)
      ce::detail::spin_pause();
  } else
    std::this_thread::yield();
}
static void poll() noexcept { wait_policy(polling_pause); }
static void poll_start() noexcept { wait_policy(startup_pause); }
struct trial {
  double ns = 0, worker_start_max_ns = 0;
  uint64_t push_retries = 0, pop_retries = 0;
};
template <class Queue> trial transfer(unsigned producers) {
  constexpr uint64_t each = 262144;
  auto q = std::make_unique<Queue>();
  std::atomic<bool> start = false;
  std::atomic<unsigned> ready = 0;
  std::vector<uint64_t> retries(producers, 0);
  std::vector<clock_type::time_point> started(producers);
  std::vector<std::thread> threads;
  for (unsigned p = 0; p < producers; ++p)
    threads.emplace_back([&, p] {
      pin_benchmark_worker(p);
      ++ready;
      while (!start.load())
        poll_start();
      started[p] = clock_type::now();
      uint64_t rejected = 0;
      for (uint64_t i = 0; i < each; ++i) {
        payload v;
        v.producer = p;
        v.sequence = i;
        for (unsigned w = 0; w < 14; ++w)
          v.words[w] = (i + 1) * (w + 3) + p;
        while (!q->push(static_cast<payload &&>(v))) {
          ++rejected;
          poll();
        }
      }
      retries[p] = rejected;
    });
  while (ready.load() != producers)
    poll_start();
  std::vector<uint64_t> expected(producers, 0);
  uint64_t sum = 0, empty = 0;
  auto before = clock_type::now();
  start = true;
  for (uint64_t i = 0; i < each * producers; ++i) {
    payload v;
    while (!q->pop(v)) {
      ++empty;
      poll();
    }
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
  trial result;
  result.ns = std::chrono::duration<double, std::nano>(after - before).count();
  result.pop_retries = empty;
  for (unsigned p = 0; p < producers; ++p) {
    result.push_retries += retries[p];
    result.worker_start_max_ns = std::max(
        result.worker_start_max_ns,
        std::chrono::duration<double, std::nano>(started[p] - before).count());
  }
  return result;
}
template <size_t Cap> void compare(unsigned producers) {
  std::vector<double> a, b;
  a.reserve(15);
  b.reserve(15);
  for (unsigned s = 0; s < 18; ++s) {
    trial ax[32]{}, by[32]{};
    double x = 0, y = 0;
    for (unsigned sub = 0; sub < batch_repeats; ++sub) {
      if ((s + sub) & 1) {
        by[sub] = transfer<ppl_queue>(producers);
        ax[sub] = transfer<custom_queue<Cap>>(producers);
      } else {
        ax[sub] = transfer<custom_queue<Cap>>(producers);
        by[sub] = transfer<ppl_queue>(producers);
      }
      x += ax[sub].ns;
      y += by[sub].ns;
    }
    for (unsigned sub = 0; sub < batch_repeats; ++sub) {
      const auto &u = ax[sub];
      const auto &v = by[sub];
      std::printf(
          "mpsc_subraw,%zu,%u,%d,%u,%.0f,%.0f,%llu,%llu,%llu,%llu,%.0f,%.0f\n",
          Cap, producers, int(s) - 3, sub, u.ns, v.ns,
          (unsigned long long)u.push_retries, (unsigned long long)u.pop_retries,
          (unsigned long long)v.push_retries, (unsigned long long)v.pop_retries,
          u.worker_start_max_ns, v.worker_start_max_ns);
    }
    std::printf("mpsc_batch_total,%zu,%u,%d,%u,%.0f,%.0f\n", Cap, producers,
                int(s) - 3, batch_repeats, x, y);
    x /= batch_repeats;
    y /= batch_repeats;
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
  polling_pause = benchmark_flag("CE_MPSC_PAUSE");
  startup_pause = benchmark_flag("CE_MPSC_START_PAUSE");
  char value[32]{};
  if (GetEnvironmentVariableA("CE_MPSC_BATCH_REPEATS", value, sizeof(value))) {
    char *end = nullptr;
    auto n = std::strtoul(value, &end, 10);
    check(end && *end == '\0' && n >= 1 && n <= 32);
    batch_repeats = unsigned(n);
  }
  std::printf("mpsc_policy,retry,%s,start,%s,batch_repeats,%u,items_per_"
              "producer,262144\n",
              polling_pause ? "cpu_pause32" : "os_yield",
              startup_pause ? "cpu_pause32" : "os_yield", batch_repeats);
  for (unsigned p : {1u, 4u}) {
    compare<64>(p);
    compare<1024>(p);
  }
}
