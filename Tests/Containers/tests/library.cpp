#include <algorithm>
#include <ce/age_buckets.h>
#include <ce/containers.h>
#include <ce/inplace_function.h>
#include <ce/intrusive_lru.h>
#include <ce/radix_sort.h>
#include <ce/sequence_buffer.h>
#include <ce/slot_map.h>
#include <ce/string_atom.h>
#include <ce/tlsf_resource.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
static_assert(
    !std::is_copy_constructible_v<ce::dynamic_array<std::unique_ptr<int>>>);
static_assert(
    !std::is_copy_constructible_v<ce::flat_hash_map<std::unique_ptr<int>>>);
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "LIBRARY_FAIL line %d: %s\n", __LINE__, #x);        \
      std::abort();                                                            \
    }                                                                          \
  } while (false)
static uint64_t state = 72025;
static uint64_t random64() {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}
struct resource final : ce::memory_resource {
  struct allocation {
    size_t bytes, alignment;
  };
  std::unordered_map<void *, allocation> live;
  size_t calls = 0, fail_at = SIZE_MAX;
  void *allocate(size_t n, size_t a) override {
    if (calls++ == fail_at)
      throw std::bad_alloc();
    auto p = ce::detail::heap_allocate(n, a);
    live.emplace(p, allocation{n, a});
    return p;
  }
  void deallocate(void *p, size_t n, size_t a) noexcept override {
    auto it = live.find(p);
    CHECK(it != live.end() && it->second.bytes == n &&
          it->second.alignment == a);
    live.erase(it);
    ce::detail::heap_deallocate(p, a);
  }
  ~resource() { CHECK(live.empty()); }
};
struct tracked {
  static inline int live = 0, budget = -1;
  int value;
  explicit tracked(int v = 0) : value(v) {
    if (budget == 0)
      throw 1;
    if (budget > 0)
      --budget;
    ++live;
  }
  tracked(const tracked &o) : tracked(o.value) {}
  tracked(tracked &&o) noexcept(false) : tracked(o.value) { o.value = -1; }
  tracked &operator=(const tracked &o) {
    if (budget == 0)
      throw 1;
    if (budget > 0)
      --budget;
    value = o.value;
    return *this;
  }
  tracked &operator=(tracked &&o) noexcept(false) {
    if (budget == 0)
      throw 1;
    value = o.value;
    o.value = -1;
    return *this;
  }
  ~tracked() noexcept { --live; }
};
struct realloc_state {
  bool fail = false;
  size_t calls = 0;
};
struct test_reallocator : ce::heap_allocator {
  using byte_relocation_allocator = test_reallocator;
  realloc_state *state;
  explicit test_reallocator(realloc_state &s) noexcept : state(&s) {}
  void *reallocate(void *p, size_t old_bytes, size_t new_bytes,
                   size_t alignment) {
    ++state->calls;
    if (state->fail)
      throw std::bad_alloc();
    return ce::detail::heap_reallocate(p, old_bytes, new_bytes, alignment);
  }
};
struct derived_tracked_allocator : ce::heap_allocator {
  resource *owner;
  explicit derived_tracked_allocator(resource &r) noexcept : owner(&r) {}
  void *allocate(size_t n, size_t a) { return owner->allocate(n, a); }
  void deallocate(void *p, size_t n, size_t a) noexcept {
    owner->deallocate(p, n, a);
  }
};
static void arrays() {
  {
    resource r;
    {
      ce::dynamic_array<int, derived_tracked_allocator> a{
          derived_tracked_allocator(r)};
      a.reserve(1);
      a.push_back(7);
      a.push_back(a[0]);
      CHECK(r.calls == 2 && a.size() == 2 && a[0] == 7 && a[1] == 7);
    }
    CHECK(r.live.empty());
  }
  {
    realloc_state allocation_state;
    test_reallocator allocator(allocation_state);
    ce::dynamic_array<int, test_reallocator> a(allocator);
    a.reserve(1);
    a.push_back(42);
    auto address = a.data();
    allocation_state.fail = true;
    try {
      a.push_back(a[0]);
      CHECK(false);
    } catch (const std::bad_alloc &) {
    }
    CHECK(a.size() == 1 && a[0] == 42 && a.data() == address &&
          a.capacity() == 1);
    allocation_state.fail = false;
    a.push_back(a[0]);
    CHECK(a.size() == 2 && a[1] == 42);
    auto calls = allocation_state.calls;
    ce::dynamic_array<ce::utf8_string, test_reallocator> nontrivial(allocator);
    nontrivial.reserve(1);
    nontrivial.emplace_back(ce::utf8_string_view("alias"));
    nontrivial.emplace_back(nontrivial[0]);
    CHECK(allocation_state.calls == calls && nontrivial[0] == nontrivial[1]);
    struct alignas(128) aligned {
      uint64_t value;
      unsigned char padding[120];
    };
    ce::dynamic_array<aligned> over;
    over.push_back({99, {}});
    over.reserve(1000);
    CHECK(uintptr_t(over.data()) % 128 == 0 && over[0].value == 99);
  }
  resource one, two;
  ce::resource_allocator a(one), b(two);
  {
    ce::dynamic_array<tracked, ce::resource_allocator> v(a);
    for (int i = 0; i < 16; ++i)
      v.emplace_back(i);
    auto live = tracked::live;
    tracked::budget = 3;
    bool caught = false;
    try {
      v.reserve(100);
    } catch (int) {
      caught = true;
    }
    tracked::budget = -1;
    CHECK(caught && v.size() == 16 && tracked::live == live);
    for (int i = 0; i < 16; ++i)
      CHECK(v[size_t(i)].value == i);
    one.fail_at = one.calls;
    try {
      v.reserve(100);
      CHECK(false);
    } catch (const std::bad_alloc &) {
    }
    one.fail_at = SIZE_MAX;
    CHECK(v.size() == 16);
    auto copy = v;
    copy.emplace(3, 123);
    CHECK(copy[3].value == 123);
    copy.erase(3);
    for (int i = 0; i < 16; ++i)
      CHECK(copy[size_t(i)].value == i);
    ce::dynamic_array<tracked, ce::resource_allocator> w(b);
    w.emplace_back(99);
    w.swap(copy);
    CHECK(copy[0].value == 99 && w[15].value == 15);
    w = std::move(v);
    CHECK(v.empty() && w.size() == 16);
    w.resize(25);
    w.resize(2);
    CHECK(w.size() == 2);
    w.clear();
    CHECK(w.empty());
  }
  CHECK(tracked::live == 0);
  {
    ce::dynamic_array<std::unique_ptr<int>> v;
    for (int i = 0; i < 1000; ++i)
      v.push_back(std::make_unique<int>(i));
    v.reserve(3000);
    v.erase(10, 100);
    CHECK(*v[10] == 110);
    v.insert(2, std::make_unique<int>(999));
    CHECK(*v[2] == 999);
  }
  {
    ce::dynamic_array<ce::utf8_string> v;
    v.reserve(1);
    v.emplace_back(ce::utf8_string_view("alias"));
    v.emplace_back(v[0]);
    CHECK(v[0].view() == ce::utf8_string_view("alias") &&
          v[1].view() == v[0].view());
  }
  {
    ce::bounded_ring<std::unique_ptr<int>> q(2);
    q.push(std::make_unique<int>(1));
    q.emplace(std::make_unique<int>(2));
    auto value = std::make_unique<int>(3);
    CHECK(!q.push(std::move(value)) && value);
    std::unique_ptr<int> out;
    CHECK(q.pop(out) && *out == 1);
    CHECK(q.push(std::move(value)) && !value);
    CHECK(q.pop(out) && *out == 2);
    CHECK(q.pop(out) && *out == 3);
  }
  {
    alignas(128) unsigned char storage[4096];
    ce::fixed_buffer_resource fixed(storage, sizeof(storage));
    auto p = fixed.allocate(16, 128);
    CHECK(uintptr_t(p) % 128 == 0);
    try {
      fixed.allocate(8192, 1);
      CHECK(false);
    } catch (const std::bad_alloc &) {
    }
    fixed.reset();
    CHECK(fixed.used() == 0);
  }
  {
    ce::monotonic_resource mono(256, &one);
    {
      ce::dynamic_array<std::string, ce::resource_allocator> v{
          ce::resource_allocator(mono)};
      for (int i = 0; i < 100; ++i)
        v.emplace_back(100, 'x');
    }
    mono.release();
    CHECK(one.live.empty());
  }
}
static void strings() {
  ce::utf8_string a;
  std::string b;
  for (unsigned step = 0; step < 50000; ++step) {
    if (b.size() > 4096) {
      a.clear();
      b.clear();
    }
    switch (random64() % 7) {
    case 0: {
      char ch = char(random64());
      a.push_back(ch);
      b.push_back(ch);
      break;
    }
    case 1: {
      std::string text(size_t(random64() % 40), char(random64()));
      a.append({text.data(), text.size()});
      b.append(text);
      break;
    }
    case 2: {
      size_t p = size_t(random64() % (b.size() + 1)),
             n = size_t(random64() % 40);
      a.erase(p, n);
      b.erase(p, n);
      break;
    }
    case 3: {
      size_t p = size_t(random64() % (b.size() + 1));
      std::string text(size_t(random64() % 20), 'q');
      a.insert(p, {text.data(), text.size()});
      b.insert(p, text);
      break;
    }
    case 4: {
      size_t n = size_t(random64() % 200);
      a.resize(n, 'v');
      b.resize(n, 'v');
      break;
    }
    case 5: {
      size_t p = size_t(random64() % (b.size() + 1));
      a.assign(a.view().substr(p));
      b = b.substr(p);
      break;
    }
    default: {
      auto c = a;
      ce::utf8_string moved(std::move(c));
      CHECK(c.empty() && moved.view() == a.view());
      a = std::move(moved);
      break;
    }
    }
    CHECK(a.size() == b.size() && memcmp(a.data(), b.data(), b.size()) == 0 &&
          a.c_str()[a.size()] == 0);
    std::string needle(size_t(random64() % 5), char(random64()));
    size_t pos = size_t(random64() % (b.size() + 2));
    CHECK(a.find({needle.data(), needle.size()}, pos) == b.find(needle, pos));
  }
  resource r;
  {
    ce::basic_utf8_string<ce::resource_allocator> x(
        ce::utf8_string_view("a long string beyond small capacity"),
        ce::resource_allocator(r));
    auto copy = x;
    x.append(x.view());
    CHECK(x.size() == copy.size() * 2);
    auto y = std::move(x);
    CHECK(x.empty());
    y.insert(1, y.view().substr(2, 4));
    y.erase(0, 3);
  }
  CHECK(ce::hash<float>{}(0.0f) == ce::hash<float>{}(-0.0f));
}
struct constant_hash {
  uint64_t operator()(uint64_t) const noexcept { return 0; }
};
static void maps() {
  {
    struct end_hash {
      uint64_t operator()(uint64_t) const noexcept { return UINT64_MAX; }
    };
    ce::basic_flat_hash_map<uint64_t, int, end_hash> wrapped;
    for (uint64_t i = 0; i < 40; ++i)
      wrapped.try_emplace(i, int(i));
    for (uint64_t i = 0; i < 40; i += 3)
      CHECK(wrapped.erase(i));
    wrapped.reserve(1024);
    auto copy = wrapped;
    for (uint64_t i = 0; i < 40; ++i) {
      auto p = copy.find(i);
      CHECK(bool(p) == bool(i % 3));
      if (p)
        CHECK(*p == int(i));
    }
    CHECK(!copy.contains(uint64_t(9999)));
    wrapped.clear();
    wrapped.try_emplace(uint64_t(9999), 77);
    CHECK(wrapped.size() == 1 && *wrapped.find(uint64_t(9999)) == 77);
  }
  ce::basic_flat_hash_map<ce::utf8_string, std::unique_ptr<int>> m;
  std::unordered_map<std::string, int> ref;
  for (unsigned i = 0; i < 120000; ++i) {
    std::string key = "key" + std::to_string(random64() % 1024);
    ce::utf8_string_view view(key.data(), key.size());
    switch (random64() % 3) {
    case 0: {
      int v = int(random64() % 100000);
      m.insert_or_assign(ce::utf8_string(view), std::make_unique<int>(v));
      ref.insert_or_assign(key, v);
      break;
    }
    case 1:
      CHECK(m.erase(view) == (ref.erase(key) != 0));
      break;
    default: {
      auto p = m.find(view);
      auto it = ref.find(key);
      CHECK(bool(p) == (it != ref.end()));
      if (p)
        CHECK(**p == it->second);
    }
    }
    CHECK(m.size() == ref.size());
  }
  size_t iterated = 0;
  for (auto &e : m) {
    auto key = e.key().view();
    auto it = ref.find(std::string(key.data(), key.size()));
    CHECK(it != ref.end() && *e.value() == it->second);
    ++iterated;
  }
  CHECK(iterated == ref.size());
  {
    ce::basic_flat_hash_map<uint64_t, int, constant_hash> collision;
    for (uint64_t i = 0; i < 3000; ++i)
      collision.insert_or_assign(i, int(i));
    for (uint64_t i = 0; i < 3000; i += 2)
      CHECK(collision.erase(i));
    for (uint64_t i = 0; i < 3000; ++i)
      CHECK(bool(collision.find(i)) == bool(i & 1));
    for (uint64_t i = 3000; i < 6000; ++i)
      collision.try_emplace(i, int(i));
  }
  {
    ce::flat_hash_map<tracked> v;
    for (uint64_t i = 0; i < 16; ++i)
      v.try_emplace(i, int(i));
    tracked::budget = 4;
    try {
      v.reserve(1000);
      CHECK(false);
    } catch (int) {
    }
    tracked::budget = -1;
    CHECK(v.size() == 16);
    for (uint64_t i = 0; i < 16; ++i)
      CHECK(v.find(i) && v.find(i)->value == int(i));
    auto copy = v;
    CHECK(copy.size() == 16);
  }
  CHECK(tracked::live == 0);
  {
    ce::flat_hash_set<ce::utf8_string> s;
    CHECK(s.insert(ce::utf8_string(ce::utf8_string_view("one"))));
    CHECK(!s.insert(ce::utf8_string(ce::utf8_string_view("one"))));
    CHECK(s.contains(ce::utf8_string_view("one")));
    for (const auto &key : s)
      CHECK(key.view() == ce::utf8_string_view("one"));
    CHECK(s.erase(ce::utf8_string_view("one")));
  }
  for (size_t fail = 0; fail < 6; ++fail) {
    resource r;
    {
      ce::flat_hash_map<tracked, ce::resource_allocator> v{
          ce::resource_allocator(r)};
      for (uint64_t i = 0; i < 4; ++i)
        v.try_emplace(i, int(i));
      r.fail_at = r.calls + fail;
      try {
        v.try_emplace(9, 9);
      } catch (const std::bad_alloc &) {
      }
      r.fail_at = SIZE_MAX;
      for (uint64_t i = 0; i < 4; ++i)
        CHECK(v.find(i) && v.find(i)->value == int(i));
    }
  }
  CHECK(tracked::live == 0);
}
static void slots() {
  ce::slot_map<std::unique_ptr<int>> s;
  using H = decltype(s)::handle;
  std::vector<H> live, stale;
  std::vector<std::unique_ptr<int> *> addresses;
  for (unsigned i = 0; i < 60000; ++i) {
    if (live.empty() || random64() % 3) {
      auto h = s.emplace(std::make_unique<int>(int(i)));
      live.push_back(h);
      addresses.push_back(s.find(h));
    } else {
      auto k = size_t(random64() % live.size());
      stale.push_back(live[k]);
      CHECK(s.erase(live[k]));
      live[k] = live.back();
      live.pop_back();
      addresses[k] = addresses.back();
      addresses.pop_back();
    }
    if (i % 1000 == 0)
      for (size_t k = 0; k < live.size(); ++k)
        CHECK(s.find(live[k]) == addresses[k]);
  }
  for (auto h : stale)
    CHECK(!s.find(h) && !s.erase(h));
  auto h = live.front();
  s.clear();
  CHECK(!s.find(h) && s.size() == 0);
  ce::slot_map<std::unique_ptr<int>> other;
  CHECK(!other.find(h));
  {
    ce::slot_map<tracked> v;
    tracked::budget = 0;
    try {
      v.emplace(1);
      CHECK(false);
    } catch (int) {
    }
    tracked::budget = -1;
    auto id = v.emplace(2);
    CHECK(id.index == 0 && v.at(id).value == 2);
  }
  CHECK(tracked::live == 0);
}
static void sequences() {
  ce::sequence_buffer<uint64_t, 16> s;
  std::map<uint64_t, uint64_t> ref;
  uint64_t newest = 0;
  for (unsigned i = 0; i < 100000; ++i) {
    auto sequence = newest + (random64() % 5);
    if (random64() % 3 == 0 && newest > 40)
      sequence = newest - (random64() % 40);
    auto value = random64();
    bool rejected = sequence < newest && newest - sequence >= 16;
    auto p = s.emplace(sequence, value);
    CHECK(bool(p) != rejected);
    if (!rejected) {
      if (sequence > newest)
        newest = sequence;
      for (auto it = ref.begin(); it != ref.end();)
        if (newest - it->first >= 16)
          it = ref.erase(it);
        else
          ++it;
      ref[sequence] = value;
    }
    CHECK(s.size() == ref.size());
    for (auto [key, v] : ref)
      CHECK(s.find(key) && *s.find(key) == v);
  }
  s.clear();
  CHECK(s.emplace(UINT64_MAX - 1, 1));
  CHECK(s.emplace(UINT64_MAX, 2));
  CHECK(s.find(UINT64_MAX) && *s.find(UINT64_MAX) == 2);
}
static void lru() {
  ce::lru_link<> links[64];
  ce::intrusive_lru<> lru(links, 64);
  std::list<uint32_t> ref;
  for (unsigned i = 0; i < 100000; ++i) {
    auto key = uint32_t(random64() % 64);
    auto it = std::find(ref.begin(), ref.end(), key);
    switch (random64() % 4) {
    case 0:
      CHECK(lru.push_front(key) == (it == ref.end()));
      if (it == ref.end())
        ref.push_front(key);
      break;
    case 1:
      CHECK(lru.erase(key) == (it != ref.end()));
      if (it != ref.end())
        ref.erase(it);
      break;
    case 2:
      CHECK(lru.touch(key) == (it != ref.end()));
      if (it != ref.end()) {
        ref.erase(it);
        ref.push_front(key);
      }
      break;
    default: {
      auto actual = lru.pop_back();
      if (ref.empty())
        CHECK(actual == UINT32_MAX);
      else {
        CHECK(actual == ref.back());
        ref.pop_back();
      }
    }
    }
    CHECK(lru.size() == ref.size());
    CHECK(lru.front() == (ref.empty() ? UINT32_MAX : ref.front()));
  }
}
static void atoms() {
  resource r;
  {
    ce::basic_string_atom_table<ce::resource_allocator> table{
        ce::resource_allocator(r)};
    auto initial = table.intern(ce::utf8_string_view("stable"));
    auto pointer = table.at(initial).data();
    for (unsigned i = 0; i < 5000; ++i) {
      auto key = std::to_string(i);
      table.intern({key.data(), key.size()});
    }
    CHECK(table.at(initial).data() == pointer);
    auto calls = r.calls;
    CHECK(table.intern(ce::utf8_string_view("stable")) == initial &&
          r.calls == calls);
    auto binary = table.intern({"a\0b", 3});
    CHECK(table.at(binary).size() == 3);
    table.clear();
    ce::utf8_string_view out;
    CHECK(!table.resolve(initial, out));
  }
  for (size_t failure = 0; failure < 8; ++failure) {
    resource failing;
    ce::basic_string_atom_table<ce::resource_allocator> table{
        ce::resource_allocator(failing)};
    auto kept = table.intern(ce::utf8_string_view("keep"));
    failing.fail_at = failing.calls + failure;
    try {
      table.intern(ce::utf8_string_view("allocation failure candidate"));
    } catch (const std::bad_alloc &) {
    }
    failing.fail_at = SIZE_MAX;
    CHECK(table.at(kept) == ce::utf8_string_view("keep"));
    auto inserted =
        table.intern(ce::utf8_string_view("allocation failure candidate"));
    CHECK(table.at(inserted) ==
          ce::utf8_string_view("allocation failure candidate"));
    CHECK(table.size() == 2);
  }
}
static int add(int x) { return x + 1; }
static void functions_and_sort() {
  ce::inplace_function<int(int)> f([](int x) { return x + 2; });
  CHECK(f(3) == 5);
  auto moved = std::move(f);
  CHECK(!f && moved(4) == 6);
  ce::inplace_function<int()> owner(
      [p = std::make_unique<int>(7)] { return *p; });
  CHECK(owner() == 7);
  owner.reset();
  try {
    owner();
    CHECK(false);
  } catch (const ce::bad_function_call &) {
  }
  ce::inplace_function<int(int)> pointer(&add);
  CHECK(pointer(1) == 2);
  ce::inplace_function<int(int)> named(add);
  CHECK(named(1) == 2);
  ce::inplace_function<int(int)> null_pointer(
      static_cast<int (*)(int)>(nullptr));
  CHECK(!null_pointer);
  int invoked = 0;
  ce::inplace_function<void()> discard([&] { return ++invoked; });
  discard();
  CHECK(invoked == 1);
  struct record {
    uint64_t key;
    uint32_t order;
  };
  std::vector<record> a;
  for (uint32_t i = 0; i < 20000; ++i)
    a.push_back({random64() % 128, i});
  auto b = a;
  ce::radix_sort(a.data(), a.size(),
                 [](const record &r) noexcept { return r.key; });
  std::stable_sort(b.begin(), b.end(),
                   [](auto &x, auto &y) { return x.key < y.key; });
  for (size_t i = 0; i < a.size(); ++i)
    CHECK(a[i].key == b[i].key && a[i].order == b[i].order);
  struct owned_record {
    uint32_t key;
    std::unique_ptr<uint32_t> order;
  };
  for (size_t count : {size_t(0), size_t(1), size_t(2), size_t(16), size_t(17),
                       size_t(31), size_t(32), size_t(127), size_t(256),
                       size_t(511), size_t(1023), size_t(1024), size_t(4097)}) {
    std::vector<owned_record> owned;
    std::vector<std::pair<uint32_t, uint32_t>> expected;
    for (uint32_t i = 0; i < count; ++i) {
      auto k = uint32_t(random64() % 31);
      owned.push_back({k, std::make_unique<uint32_t>(i)});
      expected.emplace_back(k, i);
    }
    std::stable_sort(expected.begin(), expected.end(),
                     [](auto a, auto b) { return a.first < b.first; });
    ce::radix_sort(owned.data(), owned.size(),
                   [](const owned_record &v) noexcept { return v.key; });
    for (size_t i = 0; i < count; ++i)
      CHECK(owned[i].key == expected[i].first &&
            *owned[i].order == expected[i].second);
  }
}
static void timed_resources() {
  ce::age_buckets<uint64_t, 128> ages;
  std::multimap<uint64_t, uint64_t> expected;
  uint64_t now = 0;
  for (uint64_t i = 0; i < 50000; ++i) {
    auto deadline = now + random64() % 128;
    CHECK(ages.emplace(deadline, i));
    expected.emplace(deadline, i);
    CHECK(!ages.emplace(now + 128, i));
    if (i % 3 == 0) {
      auto through = now + random64() % 180;
      ages.expire_through(through,
                          [&](uint64_t &value, uint64_t tick) noexcept {
                            CHECK(!expected.empty());
                            CHECK(expected.begin()->first == tick &&
                                  expected.begin()->second == value);
                            expected.erase(expected.begin());
                          });
      now = through + 1;
    }
    CHECK(ages.size() == expected.size());
  }
  ages.expire_through(UINT64_MAX, [](auto &, uint64_t) noexcept {});
  CHECK(!ages.size() && !ages.emplace(UINT64_MAX, 1));
  struct alignas(128) storage {
    unsigned char bytes[1024 * 1024];
  };
  auto owner = std::make_unique<storage>();
  auto &arena = owner->bytes;
  ce::tlsf_resource tlsf(arena, sizeof(arena));
  struct record {
    void *p;
    size_t size, alignment;
    unsigned char value;
  };
  std::vector<record> live;
  for (size_t step = 0; step < 100000; ++step) {
    if (live.size() < 160 && (live.empty() || random64() % 3)) {
      auto bytes = size_t(random64() % 2048 + 1);
      auto alignment = size_t(1) << (random64() % 12);
      auto value = static_cast<unsigned char>(random64());
      auto p = tlsf.allocate(bytes, alignment);
      CHECK(uintptr_t(p) % alignment == 0);
      memset(p, value, bytes);
      live.push_back({p, bytes, alignment, value});
    } else {
      auto index = size_t(random64() % live.size());
      auto r = live[index];
      auto p = static_cast<unsigned char *>(r.p);
      for (size_t j = 0; j < r.size; ++j)
        CHECK(p[j] == r.value);
      tlsf.deallocate(r.p, r.size, r.alignment);
      live[index] = live.back();
      live.pop_back();
    }
    CHECK(tlsf.active_allocations() == live.size());
  }
  for (auto r : live)
    tlsf.deallocate(r.p, r.size, r.alignment);
  CHECK(tlsf.active_allocations() == 0);
  auto whole = tlsf.allocate(sizeof(arena) - 512, 16);
  try {
    tlsf.allocate(1024, 16);
    CHECK(false);
  } catch (const std::bad_alloc &) {
  }
  tlsf.deallocate(whole, sizeof(arena) - 512, 16);
  CHECK(tlsf.allocate(0, 1) == nullptr);
  try {
    tlsf.allocate(SIZE_MAX, 128);
    CHECK(false);
  } catch (const std::bad_alloc &) {
  }
  {
    ce::dynamic_array<ce::utf8_string, ce::resource_allocator> array{
        ce::resource_allocator(tlsf)};
    for (size_t i = 0; i < 1000; ++i)
      array.emplace_back(ce::utf8_string_view("owned"));
  }
  CHECK(tlsf.active_allocations() == 0);
}
int main() {
  arrays();
  strings();
  maps();
  slots();
  sequences();
  lru();
  atoms();
  functions_and_sort();
  timed_resources();
  CHECK(tracked::live == 0);
  std::puts("LIBRARY_CORRECTNESS_PASS");
}
