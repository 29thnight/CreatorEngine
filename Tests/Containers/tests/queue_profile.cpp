#if defined(CE_USE_FAA)
#include <ce/faa_lock_free_queue.h>
#define lock_free_queue faa_lock_free_queue
#else
#include <ce/lock_free_queue.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <intrin.h>
#include <thread>
#include <vector>
struct stats {
  uint64_t objects = 0, segments = 0, released_segments = 0;
  uint64_t calls[ce::detail::event_count]{}, samples[ce::detail::event_count]{},
      cycles[ce::detail::event_count]{}, failures[ce::detail::event_count]{};
};
struct measured_hooks {
  static inline thread_local stats local;
  static uint64_t ticks() noexcept {
    unsigned aux;
    _ReadWriteBarrier();
    _mm_lfence();
    auto stamp = __rdtscp(&aux);
    _mm_lfence();
    _ReadWriteBarrier();
    return stamp;
  }
  static uint64_t begin(ce::detail::queue_event e) noexcept {
    auto n = ++local.calls[e];
    return (n & 63) == 0 ? ticks() : 0;
  }
  static void end(ce::detail::queue_event e, uint64_t start) noexcept {
    if (start) {
      local.cycles[e] += ticks() - start;
      ++local.samples[e];
    }
  }
  static void failure(ce::detail::queue_event e) noexcept {
    ++local.failures[e];
  }
  static void after_link() noexcept {}
  static void after_head_protected() noexcept {}
  static void node_created() noexcept { ++local.objects; }
  static void node_destroyed() noexcept {}
  static void segment_created() noexcept { ++local.segments; }
  static void segment_destroyed() noexcept { ++local.released_segments; }
};
struct value {
#if defined(CE_PROFILE_SMALL)
  uint64_t x[1]{};
#else
  uint64_t x[16]{};
#endif
};
int main() {
#if defined(CE_USE_FAA)
  using queue = ce::lock_free_queue<value, 64, measured_hooks>;
  std::printf(
      "sizes,value,%zu,node,%zu,block,%zu,segment,%zu,queue,%zu,slots,%zu\n",
      sizeof(value), queue::value_storage_bytes, queue::block_storage_bytes,
      queue::segment_storage_bytes, sizeof(queue), queue::block_slot_count);
#endif
  for (auto shape : {std::pair{1u, 1u}, std::pair{4u, 1u}, std::pair{4u, 4u}}) {
    auto [p, c] = shape;
    ce::lock_free_queue<value, 64, measured_hooks> q;
    std::atomic<bool> go{false};
    std::atomic<unsigned> ready{0}, done{0};
    std::vector<std::thread> threads;
    std::vector<stats> results(p + c);
    std::vector<uint64_t> counts(c);
    constexpr unsigned count = 100000;
    for (unsigned i = 0; i < c; ++i)
      threads.emplace_back([&, i] {
        measured_hooks::local = {};
        ++ready;
        while (!go.load())
          std::this_thread::yield();
        value v;
        uint64_t n = 0;
        for (;;) {
          bool finished = done.load() == p;
          if (q.try_pop(v)) {
            ++n;
            if (n % 64 == 0)
              q.reclaim();
          } else if (finished)
            break;
          else
            std::this_thread::yield();
        }
        counts[i] = n;
        results[i] = measured_hooks::local;
      });
    for (unsigned i = 0; i < p; ++i)
      threads.emplace_back([&, i] {
        measured_hooks::local = {};
        ++ready;
        while (!go.load())
          std::this_thread::yield();
        for (unsigned n = 0; n < count; ++n) {
          value v;
          v.x[0] = n;
          q.push(static_cast<value &&>(v));
        }
        ++done;
        results[c + i] = measured_hooks::local;
      });
    while (ready.load() != p + c)
      std::this_thread::yield();
    auto start = std::chrono::steady_clock::now();
    go = true;
    for (auto &t : threads)
      t.join();
    auto end = std::chrono::steady_clock::now();
    uint64_t total = 0;
    for (auto n : counts)
      total += n;
    if (total != uint64_t(p) * count)
      return 1;
    uint64_t objs = 0, segs = 0, released = 0;
    for (auto &s : results) {
      objs += s.objects;
      segs += s.segments;
      released += s.released_segments;
    }
    std::printf("objects,%u,%u,%llu,%llu,%llu\n", p, c,
                (unsigned long long)objs, (unsigned long long)segs,
                (unsigned long long)released);
    std::printf("wall,%u,%u,%.2f\n", p, c,
                std::chrono::duration<double, std::nano>(end - start).count() /
                    double(total));
    for (int e = 0; e < ce::detail::event_count; ++e) {
      uint64_t calls = 0, samples = 0, cycles = 0, failures = 0;
      for (auto &s : results) {
        calls += s.calls[e];
        samples += s.samples[e];
        cycles += s.cycles[e];
        failures += s.failures[e];
      }
      std::printf("event,%u,%u,%d,%llu,%llu,%.2f,%llu\n", p, c, e,
                  (unsigned long long)calls, (unsigned long long)samples,
                  samples ? double(cycles) / double(samples) : 0.,
                  (unsigned long long)failures);
    }
  }
  uint64_t overhead = 0;
  for (int i = 0; i < 10000; ++i) {
    auto a = measured_hooks::ticks();
    overhead += measured_hooks::ticks() - a;
  }
  std::printf("timer_pair_cycles,%.2f\n", double(overhead) / 10000.);
}
