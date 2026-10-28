#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "context.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
inline DWORD_PTR benchmark_measuring_mask = 0;
inline DWORD_PTR benchmark_worker_masks[64]{};
inline unsigned benchmark_worker_count = 0;
#endif
#ifndef CE_BENCH_HAS_CHECK
static void check(bool b) {
  if (!b)
    std::abort();
}
#endif
struct bench_diagnostic {
  double cpu_ns = 0;
  uint64_t cycles = 0;
  array_context context{};
};
static bench_diagnostic last_diagnostic;
static bool benchmark_flag(const char *name) {
#if defined(_WIN32)
  return GetEnvironmentVariableA(name, nullptr, 0) != 0;
#else
  return std::getenv(name) != nullptr;
#endif
}
static bool controlled_benchmark() {
  return benchmark_flag("CE_BENCH_CONTROLLED");
}
static double target_batch_ns() {
#if defined(_WIN32)
  char value[32]{};
  if (GetEnvironmentVariableA("CE_BENCH_BATCH_NS", value, sizeof(value))) {
    char *end = nullptr;
    auto ns = std::strtoull(value, &end, 10);
    check(end && *end == '\0' && ns >= 20000000ULL && ns <= 1000000000ULL);
    return double(ns);
  }
#endif
  return 20000000.0;
}
inline bool selected_case(const char *name, size_t n) {
#if defined(_WIN32)
  char selected[128]{}, id[128]{};
  if (GetEnvironmentVariableA("CE_BENCH_ONLY_CASE", selected,
                              sizeof(selected))) {
    std::snprintf(id, sizeof(id), "%s,%zu", name, n);
    return std::strcmp(id, selected) == 0;
  }
#endif
  return true;
}
static bool legacy_benchmark() { return benchmark_flag("CE_BENCH_LEGACY"); }
static void setup_benchmark() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;
#if defined(_WIN32)
  if (controlled_benchmark() || benchmark_flag("CE_BENCH_AFFINITY")) {
    DWORD_PTR process_mask = 0, system_mask = 0;
    check(GetProcessAffinityMask(GetCurrentProcess(), &process_mask,
                                 &system_mask) != 0);
    DWORD_PTR selected = process_mask & (~process_mask + 1);
    if (process_mask & DWORD_PTR(16))
      selected = 16;
    benchmark_measuring_mask = selected;
    // Pin only this measuring thread; leave the user's other apps unchanged.
    check(SetThreadAffinityMask(GetCurrentThread(), selected) != 0);
    std::printf("measurement_control,affinity,%llu\n",
                static_cast<unsigned long long>(selected));
  }
  if (controlled_benchmark() || benchmark_flag("CE_BENCH_PRIORITY")) {
    check(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) !=
          0);
    std::puts("measurement_control,thread_priority,above_normal");
  }
  ULONG heap_mode = 0;
  check(HeapQueryInformation(GetProcessHeap(), HeapCompatibilityInformation,
                             &heap_mode, sizeof(heap_mode), nullptr) != 0);
  if (benchmark_flag("CE_BENCH_LFH")) {
    ULONG mode = 2;
    check(HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation,
                             &mode, sizeof(mode)) != 0);
    check(HeapQueryInformation(GetProcessHeap(), HeapCompatibilityInformation,
                               &heap_mode, sizeof(heap_mode), nullptr) != 0);
  }
  std::printf("measurement_heap,compatibility,%lu,debugger,%d\n", heap_mode,
              IsDebuggerPresent());
#endif
  std::printf("measurement_mode,%s,target_batch_ns,%.0f\n",
              legacy_benchmark() ? "legacy" : "adaptive",
              legacy_benchmark() ? 2000000.0 : target_batch_ns());
}
inline void prepare_benchmark_workers() {
#if defined(_WIN32)
  if (!controlled_benchmark())
    return;
  SYSTEM_LOGICAL_PROCESSOR_INFORMATION info[256];
  DWORD bytes = sizeof(info);
  check(GetLogicalProcessorInformation(info, &bytes) != 0);
  DWORD_PTR allowed = 0, system = 0;
  check(GetProcessAffinityMask(GetCurrentProcess(), &allowed, &system) != 0);
  for (size_t i = 0; i < bytes / sizeof(info[0]); ++i) {
    if (info[i].Relationship != RelationProcessorCore ||
        (info[i].ProcessorMask & benchmark_measuring_mask))
      continue;
    auto mask = info[i].ProcessorMask & allowed;
    if (mask && benchmark_worker_count < 64)
      benchmark_worker_masks[benchmark_worker_count++] = mask & (~mask + 1);
  }
  if (!benchmark_worker_count)
    benchmark_worker_masks[benchmark_worker_count++] = benchmark_measuring_mask;
  std::reverse(benchmark_worker_masks,
               benchmark_worker_masks + benchmark_worker_count);
  for (unsigned i = 0; i < benchmark_worker_count; ++i)
    std::printf("measurement_worker,%u,affinity,%llu\n", i,
                static_cast<unsigned long long>(benchmark_worker_masks[i]));
#endif
}
inline void pin_benchmark_worker(unsigned ordinal) {
#if defined(_WIN32)
  if (!controlled_benchmark())
    return;
  check(benchmark_worker_count > 0);
  check(SetThreadAffinityMask(
            GetCurrentThread(),
            benchmark_worker_masks[ordinal % benchmark_worker_count]) != 0);
  check(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) !=
        0);
#else
  (void)ordinal;
#endif
}
static volatile uint64_t sink = 0;
using clock_type = std::chrono::steady_clock;
template <class F> double measure(F f) {
  context_begin();
#if defined(_WIN32)
  FILETIME creation, exit, k0, u0, k1, u1;
  ULONG64 cycles0 = 0, cycles1 = 0;
  check(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k0, &u0) != 0);
  check(QueryThreadCycleTime(GetCurrentThread(), &cycles0) != 0);
#endif
  std::atomic_signal_fence(std::memory_order_seq_cst);
  auto s = clock_type::now();
  auto sum = f();
  std::atomic_signal_fence(std::memory_order_seq_cst);
  auto e = clock_type::now();
#if defined(_WIN32)
  check(QueryThreadCycleTime(GetCurrentThread(), &cycles1) != 0);
  check(GetThreadTimes(GetCurrentThread(), &creation, &exit, &k1, &u1) != 0);
  auto ticks = [](FILETIME t) {
    return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  last_diagnostic = {double(ticks(k1) + ticks(u1) - ticks(k0) - ticks(u0)) *
                         100.0,
                     cycles1 - cycles0};
#endif
  last_diagnostic.context = context_end();
  sink = sum;
  return std::chrono::duration<double, std::nano>(e - s).count();
}
template <class A, class B> void bench(const char *name, size_t n, A a, B b) {
  if (!selected_case(name, n))
    return;
  setup_benchmark();
  check(a() == b());
  auto estimate = std::min(measure(a), measure(b));
  size_t rounds =
      static_cast<size_t>(2000000.0 / std::max(estimate, 100.0)) + 1;
  size_t rounds_b = rounds;
  auto aa = [&] {
    uint64_t sum = 0;
    for (size_t r = 0; r < rounds; ++r) {
      sum += a();
      std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    return sum;
  };
  auto bb = [&] {
    uint64_t sum = 0;
    for (size_t r = 0; r < rounds_b; ++r) {
      sum += b();
      std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    return sum;
  };
  if (!legacy_benchmark()) {
    // Calibrate the actual warmed batch, rather than a cold single invocation.
    if (benchmark_flag("CE_BENCH_INDEPENDENT")) {
      while (measure(aa) < target_batch_ns() && rounds < (size_t(1) << 26))
        rounds *= 2;
      while (measure(bb) < target_batch_ns() && rounds_b < (size_t(1) << 26))
        rounds_b *= 2;
    } else {
      for (;;) {
        auto actual = std::min(measure(aa), measure(bb));
        if (actual >= target_batch_ns() || rounds >= (size_t(1) << 26))
          break;
        rounds *= 2;
        rounds_b = rounds;
      }
    }
  }
  std::printf("measurement_rounds,%s,%zu,%zu,%zu\n", name, n, rounds, rounds_b);
  for (int i = 0; i < 3; ++i) {
    measure(aa);
    measure(bb);
  }
  std::vector<double> x, y;
  std::vector<bench_diagnostic> dx, dy;
  if (!benchmark_flag("CE_BENCH_UNRESERVED_STATS")) {
    x.reserve(15);
    y.reserve(15);
    dx.reserve(15);
    dy.reserve(15);
  }
  std::printf("measurement_statistics,%s,%zu,%zu,%zu,%zu,%zu\n", name, n,
              x.capacity(), y.capacity(), dx.capacity(), dy.capacity());
  if (benchmark_flag("CE_BENCH_GROUPED")) {
    std::puts("measurement_order,grouped");
    auto warm = [&](auto &work, const char *side) {
#if defined(_WIN32)
      if (benchmark_flag("CE_BENCH_RESET_HEAP")) {
        HEAP_OPTIMIZE_RESOURCES_INFORMATION info{
            HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0};
        check(HeapSetInformation(GetProcessHeap(), HeapOptimizeResources, &info,
                                 sizeof(info)) != 0);
        std::printf("measurement_heap_reset,%s,%zu,%s\n", name, n, side);
      }
#endif
      auto begin = clock_type::now();
      unsigned count = 0;
      double first = 0, last = 0;
      do {
        last = measure(work);
        if (!count)
          first = last;
        ++count;
      } while (clock_type::now() - begin < std::chrono::seconds(1));
      std::printf("warm_diag,%s,%zu,%s,%u,%.0f,%.0f\n", name, n, side, count,
                  first, last);
    };
    auto collect_a = [&] {
      warm(aa, "custom");
      for (int i = 0; i < 15; ++i) {
        x.push_back(measure(aa) / double(rounds));
        dx.push_back(last_diagnostic);
      }
    };
    auto collect_b = [&] {
      warm(bb, "stl");
      for (int i = 0; i < 15; ++i) {
        y.push_back(measure(bb) / double(rounds_b));
        dy.push_back(last_diagnostic);
      }
    };
    if (benchmark_flag("CE_BENCH_REVERSE")) {
      collect_b();
      collect_a();
    } else {
      collect_a();
      collect_b();
    }
  } else
    for (int i = 0; i < 15; ++i) {
      if (i & 1) {
        y.push_back(measure(bb) / double(rounds_b));
        dy.push_back(last_diagnostic);
        x.push_back(measure(aa) / double(rounds));
        dx.push_back(last_diagnostic);
      } else {
        x.push_back(measure(aa) / double(rounds));
        dx.push_back(last_diagnostic);
        y.push_back(measure(bb) / double(rounds_b));
        dy.push_back(last_diagnostic);
      }
    }
  for (size_t i = 0; i < x.size(); ++i) {
    std::printf("raw,%s,%zu,%zu,%.0f,%.0f\n", name, n, i, x[i], y[i]);
    std::printf(
        "sample_diag,%s,%zu,%zu,%zu,%.0f,%.0f,%.0f,%.0f,%llu,%llu,%zu\n", name,
        n, i, rounds, x[i] * double(rounds), y[i] * double(rounds_b),
        dx[i].cpu_ns, dy[i].cpu_ns,
        static_cast<unsigned long long>(dx[i].cycles),
        static_cast<unsigned long long>(dy[i].cycles), rounds_b);
  }
  for (size_t i=0;i<x.size();++i) { context_print(name,n,i,"custom",dx[i].context); context_print(name,n,i,"stl",dy[i].context); }
  std::sort(x.begin(), x.end());
  std::sort(y.begin(), y.end());
  std::printf("summary,%s,%zu,15,%.0f,%.0f,%.4f,%.4f,%.4f\n", name, n, x[7],
              y[7], x[7] / y[7], x[13] / x[1], y[13] / y[1]);
}
