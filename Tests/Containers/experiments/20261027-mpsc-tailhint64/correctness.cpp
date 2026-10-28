#include "C:/Users/idene/source/repos/CreatorContainers/tests/modules/prelude.h"
import Creator.Containers;
#include <algorithm>
#include <atomic>
// API supplied by named module
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "MPSC_FAIL %d: %s\n", __LINE__, #x);                \
      std::abort();                                                            \
    }                                                                          \
  } while (false)
struct history_op {
  bool push = false, success = false;
  uint64_t value = 0, start = 0, end = 0;
};
static bool linearize(const std::vector<history_op> &ops, uint32_t done,
                      std::vector<uint64_t> queue) {
  if (done == ((uint32_t(1) << ops.size()) - 1))
    return true;
  for (size_t i = 0; i < ops.size(); ++i) {
    if (done & (uint32_t(1) << i))
      continue;
    bool ready = true;
    for (size_t j = 0; j < ops.size(); ++j)
      if (!(done & (uint32_t(1) << j)) && ops[j].end < ops[i].start) {
        ready = false;
        break;
      }
    if (!ready)
      continue;
    auto next = queue;
    auto &op = ops[i];
    if (op.push) {
      if (op.success) {
        if (next.size() == 3)
          continue;
        next.push_back(op.value);
      }
    } else if (op.success) {
      if (next.empty() || next.front() != op.value)
        continue;
      next.erase(next.begin());
    } else if (!next.empty())
      continue;
    if (linearize(ops, done | (uint32_t(1) << i), std::move(next)))
      return true;
  }
  return false;
}
static void histories() {
  for (unsigned round = 0; round < 100; ++round) {
    ce::bounded_mpsc_queue<uint64_t, 3> q;
    std::atomic<uint64_t> clock = 1;
    std::atomic<bool> start = false;
    std::vector<history_op> ops(12);
    std::vector<std::thread> threads;
    for (unsigned p = 0; p < 2; ++p)
      threads.emplace_back([&, p] {
        while (!start.load())
          std::this_thread::yield();
        for (unsigned k = 0; k < 2; ++k) {
          auto &op = ops[p * 2 + k];
          op.push = true;
          op.value = p * 2 + k;
          op.start = clock.fetch_add(1);
          op.success = q.try_push(op.value);
          op.end = clock.fetch_add(1);
          std::this_thread::yield();
        }
      });
    threads.emplace_back([&] {
      while (!start.load())
        std::this_thread::yield();
      for (unsigned k = 0; k < 8; ++k) {
        auto &op = ops[4 + k];
        op.start = clock.fetch_add(1);
        op.success = q.try_pop(op.value);
        op.end = clock.fetch_add(1);
        std::this_thread::yield();
      }
    });
    start = true;
    for (auto &t : threads)
      t.join();
    std::sort(ops.begin(), ops.end(),
              [](auto a, auto b) { return a.start < b.start; });
    CHECK(linearize(ops, 0, {}));
  }
}
struct hooks {
  static inline thread_local bool selected = false;
  static inline std::atomic<int> mode = 0;
  static inline std::atomic<bool> entered = false, release = false;
  static void pause(int where) noexcept {
    if (selected && mode.load() == where) {
      selected = false;
      entered = true;
      while (!release.load())
        std::this_thread::yield();
    }
  }
  static void after_ticket() noexcept { pause(1); }
  static void before_publish() noexcept { pause(2); }
  static void after_publish() noexcept { pause(3); }
  static void after_pool_claim() noexcept { pause(4); }
};
static void stalled(int mode) {
  ce::bounded_mpsc_queue<uint64_t, 64, hooks> q;
  if (mode == 4) {
    CHECK(q.try_push(1));
    uint64_t out;
    CHECK(q.try_pop(out));
  }
  hooks::mode = mode;
  hooks::entered = false;
  hooks::release = false;
  std::atomic<unsigned> produced = 0;
  uint64_t out = 0;
  unsigned found = 0;
  std::thread stopped([&] {
    hooks::selected = true;
    do {
      while (!q.try_push(99))
        std::this_thread::yield();
      ++produced;
    } while (mode == 4 && hooks::selected);
  });
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!hooks::entered.load()) {
    if (mode == 4 && q.try_pop(out)) {
      CHECK(out == 99);
      ++found;
    }
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
  // The selected producer stays suspended for all these complete transfers.
  while (q.try_pop(out)) {
    CHECK(out == 99);
    ++found;
  }
  for (uint64_t i = 1000; i < 11000; ++i) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!q.try_push(i)) {
      CHECK(std::chrono::steady_clock::now() < until);
      std::this_thread::yield();
    }
    CHECK(q.try_pop(out) && out == i);
  }
  hooks::release = true;
  stopped.join();
  while (q.try_pop(out)) {
    CHECK(out == 99);
    ++found;
  }
  CHECK(found == produced.load());
  hooks::mode = 0;
}
struct throwing {
  static inline int live = 0;
  static inline bool fail = false;
  int value = 0;
  explicit throwing(int v = 0) : value(v) {
    if (fail)
      throw 1;
    ++live;
  }
  throwing(throwing &&o) noexcept : value(o.value) { ++live; }
  throwing &operator=(throwing &&o) noexcept {
    value = o.value;
    return *this;
  }
  ~throwing() noexcept { --live; }
};
struct limited : ce::detail::mpsc_no_hooks {
  static constexpr uint64_t ticket_limit = 16;
};
static void ownership() {
  ce::bounded_mpsc_queue<std::unique_ptr<int>, 1> q;
  CHECK(q.try_push(std::make_unique<int>(7)));
  auto rejected = std::make_unique<int>(9);
  CHECK(!q.try_push(std::move(rejected)) && rejected);
  std::unique_ptr<int> out;
  CHECK(q.try_pop(out) && *out == 7);
  CHECK(q.try_push(std::move(rejected)) && !rejected);
  CHECK(q.try_pop(out) && *out == 9);
  CHECK(!q.try_pop(out));
  {
    ce::bounded_mpsc_queue<throwing, 1> t;
    throwing::fail = true;
    try {
      t.try_emplace(1);
      CHECK(false);
    } catch (int) {
    }
    throwing::fail = false;
    CHECK(t.try_emplace(2));
    throwing value;
    CHECK(t.try_pop(value) && value.value == 2);
    CHECK(t.try_emplace(3));
  }
  CHECK(throwing::live == 0);
  {
    ce::bounded_mpsc_queue<throwing, 2, limited> finite;
    throwing value;
    for (int i = 0; i < 16; ++i) {
      CHECK(finite.try_emplace(i));
      CHECK(finite.try_pop(value) && value.value == i);
    }
    try {
      finite.try_emplace(17);
      CHECK(false);
    } catch (const ce::mpsc_counter_exhausted &) {
    }
    CHECK(!finite.try_pop(value));
    try {
      finite.try_emplace(18);
      CHECK(false);
    } catch (const ce::mpsc_counter_exhausted &) {
    }
  }
  CHECK(throwing::live == 0);
}
struct alignas(128) over_aligned_value {
  static inline bool bad_alignment = false;
  uint64_t value = 0;
  unsigned char padding[120]{};
  explicit over_aligned_value(uint64_t v = 0) noexcept : value(v) {
    if ((reinterpret_cast<uintptr_t>(this) % alignof(over_aligned_value)) != 0)
      bad_alignment = true;
  }
  over_aligned_value(over_aligned_value&& other) noexcept : over_aligned_value(other.value) {}
  over_aligned_value& operator=(over_aligned_value&& other) noexcept { value = other.value; return *this; }
};
static void over_aligned_storage() {
  over_aligned_value::bad_alignment = false;
  ce::bounded_mpsc_queue<over_aligned_value, 4> q;
  for (uint64_t i = 0; i < 4; ++i) CHECK(q.try_emplace(i));
  over_aligned_value out;
  for (uint64_t i = 0; i < 4; ++i) CHECK(q.try_pop(out) && out.value == i);
  CHECK(!q.try_pop(out));
  CHECK(!over_aligned_value::bad_alignment);
}
template <size_t Capacity> static void stress(unsigned producers) {
  constexpr uint64_t each = 40000;
  auto queue = std::make_unique<ce::bounded_mpsc_queue<uint64_t, Capacity>>();
  std::atomic<bool> start = false;
  std::vector<std::thread> workers;
  for (unsigned p = 0; p < producers; ++p)
    workers.emplace_back([&, p] {
      while (!start.load())
        std::this_thread::yield();
      for (uint64_t i = 0; i < each; ++i) {
        auto v = uint64_t(p) * each + i;
        while (!queue->try_push(v))
          std::this_thread::yield();
      }
    });
  std::vector<unsigned char> seen(size_t(each * producers));
  std::vector<uint64_t> next(producers, 0);
  start = true;
  for (uint64_t i = 0; i < each * producers; ++i) {
    uint64_t v;
    while (!queue->try_pop(v))
      std::this_thread::yield();
    CHECK(v < seen.size() && !seen[size_t(v)]++);
    auto p = size_t(v / each);
    CHECK(v % each == next[p]++);
  }
  for (auto &w : workers)
    w.join();
  uint64_t v;
  CHECK(!queue->try_pop(v));
}
int main() {
  histories();
  ownership();
  over_aligned_storage();
  for (int m = 1; m <= 4; ++m)
    stalled(m);
  stress<1>(4);
  stress<7>(4);
  stress<64>(4);
  stress<256>(8);
  std::puts("BOUNDED_MPSC_PASS");
}
