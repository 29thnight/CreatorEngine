#pragma once
#include "dynamic_array.h"
#include "hash.h"
#if defined(_MSC_VER)
#include <intrin.h>
#endif
namespace ce {
template <class Key, class Value, class Hash = hash<Key>,
          class Equal = equal_to<Key>, class Allocator = heap_allocator>
class basic_flat_hash_map {
  static_assert(detail::nothrow_policy<Allocator> &&
                detail::nothrow_policy<Hash> && detail::nothrow_policy<Equal>);
  static_assert(noexcept(static_cast<Key *>(nullptr)->~Key()) &&
                noexcept(static_cast<Value *>(nullptr)->~Value()));
  static_assert((detail::nothrow_move<Key> && detail::nothrow_move<Value>) ||
                    (detail::copyable<Key> && detail::copyable<Value>),
                "Rehash needs nonthrowing move or copyable key/value");
  static_assert(noexcept(static_cast<const Hash &>(*static_cast<Hash *>(
      nullptr))(static_cast<const Key &>(*static_cast<Key *>(nullptr)))));

public:
  class entry {
    Key key_;
    Value value_;
    friend class basic_flat_hash_map;
    template <class K, class... A>
    entry(K &&k, A &&...a)
        : key_(static_cast<K &&>(k)), value_(static_cast<A &&>(a)...) {}

  public:
    const Key &key() const noexcept { return key_; }
    Value &value() noexcept { return value_; }
    const Value &value() const noexcept { return value_; }
  };

private:
  entry *data_ = nullptr;
  unsigned char *control_ = nullptr;
  size_t capacity_ = 0, size_ = 0, tombstones_ = 0;
  CE_NO_UNIQUE_ADDRESS Allocator alloc_;
  CE_NO_UNIQUE_ADDRESS Hash hash_;
  CE_NO_UNIQUE_ADDRESS Equal equal_;
  static unsigned char fingerprint(uint64_t code) noexcept {
    return static_cast<unsigned char>(0x80u | (code >> 57));
  }
  static uint64_t zero_bytes(uint64_t bytes) noexcept {
    constexpr uint64_t low = 0x7f7f7f7f7f7f7f7fULL;
    // Exact per-byte high-bit mask; unlike the subtract-one test, no borrow
    // can mark a neighbouring byte as an extra candidate.
    return ~(((bytes & low) + low) | bytes | low);
  }
  static unsigned first_byte(uint64_t bits) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long index;
    _BitScanForward64(&index, bits);
    return unsigned(index >> 3);
#elif defined(__GNUC__) || defined(__clang__)
    return unsigned(__builtin_ctzll(bits) >> 3);
#else
    unsigned index = 0;
    while (!(bits & 0x80)) {
      bits >>= 8;
      ++index;
    }
    return index;
#endif
  }
  uint64_t load_group(size_t index) const noexcept {
    static_assert(sizeof(uint64_t) == 8);
    uint64_t bytes;
    memcpy(&bytes, control_ + index, 8);
    const uint16_t one = 1;
    if (*reinterpret_cast<const unsigned char *>(&one) == 0) {
      uint64_t reversed = 0;
      for (unsigned i = 0; i < 8; ++i) {
        reversed = (reversed << 8) | (bytes & 255);
        bytes >>= 8;
      }
      return reversed;
    }
    return bytes;
  }
  static void store_control(unsigned char *p, size_t cap, size_t index,
                            unsigned char value) noexcept {
    p[index] = value;
    if (index < 8)
      p[cap + index] = value;
  }
  template <class Q> CE_FORCEINLINE size_t locate(const Q &q) const noexcept {
    static_assert(noexcept(hash_(q)) && noexcept(equal_(data_[0].key_, q)),
                  "Hash and equality must be noexcept");
    if (!capacity_)
      return SIZE_MAX;
    auto mask = capacity_ - 1;
    auto code = uint64_t(hash_(q));
    auto index = size_t(code) & mask;
    if (capacity_ <= 128) {
      for (size_t n = 0; n < capacity_; ++n) {
        auto c = control_[index];
        if (!c)
          return SIZE_MAX;
        if ((c & 0x80u) && equal_(data_[index].key_, q))
          return index;
        index = (index + 1) & mask;
      }
      return SIZE_MAX;
    }
    auto tag = fingerprint(code);
    auto repeated = uint64_t(tag) * 0x0101010101010101ULL;
    for (size_t n = 0; n < capacity_; n += 8) {
      auto bytes = load_group(index);
      // Most successful lookups finish at their home slot. Preserve that
      // short path without returning to a branch for each subsequent slot.
      if ((bytes & 255u) == tag && equal_(data_[index].key_, q))
        return index;
      auto matches = zero_bytes(bytes ^ repeated) & ~uint64_t(0x80);
      auto empty = zero_bytes(bytes);
      if (empty)
        matches &= (empty & (~empty + 1)) - 1;
      while (matches) {
        auto candidate = (index + first_byte(matches)) & mask;
        if (equal_(data_[candidate].key_, q))
          return candidate;
        matches &= matches - 1;
      }
      if (empty)
        return SIZE_MAX;
      index = (index + 8) & mask;
    }
    return SIZE_MAX;
  }
  size_t vacant(uint64_t code) const noexcept {
    auto mask = capacity_ - 1;
    auto i = size_t(code) & mask;
    while (control_[i] & 0x80u)
      i = (i + 1) & mask;
    return i;
  }
  void free_storage() noexcept {
    if (data_)
      alloc_.deallocate(data_, capacity_ * sizeof(entry), alignof(entry));
    if (control_)
      alloc_.deallocate(control_, capacity_ + 8, alignof(unsigned char));
    data_ = nullptr;
    control_ = nullptr;
    capacity_ = 0;
  }
  void rehash_to(size_t cap) {
    if (cap == 0)
      return;
    auto bytes = detail::checked_bytes(cap, sizeof(entry));
    auto controls = static_cast<unsigned char *>(alloc_.allocate(cap + 8, 1));
    memset(controls, 0, cap + 8);
    entry *storage = nullptr;
    try {
      storage = static_cast<entry *>(alloc_.allocate(bytes, alignof(entry)));
      for (size_t i = 0; i < capacity_; ++i)
        if (control_[i] & 0x80u) {
          auto code = uint64_t(hash_(data_[i].key_));
          auto dst = size_t(code) & (cap - 1);
          while (controls[dst])
            dst = (dst + 1) & (cap - 1);
          if constexpr (detail::nothrow_move<Key> &&
                        detail::nothrow_move<Value>)
            ::new (static_cast<void *>(storage + dst))
                entry(static_cast<Key &&>(data_[i].key_),
                      static_cast<Value &&>(data_[i].value_));
          else
            ::new (static_cast<void *>(storage + dst))
                entry(data_[i].key_, data_[i].value_);
          store_control(controls, cap, dst, fingerprint(code));
        }
    } catch (...) {
      if (storage) {
        for (size_t i = 0; i < cap; ++i)
          if (controls[i])
            storage[i].~entry();
        alloc_.deallocate(storage, bytes, alignof(entry));
      }
      alloc_.deallocate(controls, cap + 8, 1);
      throw;
    }
    auto n = size_;
    clear();
    free_storage();
    data_ = storage;
    control_ = controls;
    capacity_ = cap;
    size_ = n;
    tombstones_ = 0;
  }
  void ensure_insert() {
    if (!capacity_)
      rehash_to(8);
    else if (size_ + tombstones_ + 1 > capacity_ / 2) {
      if (size_ + 1 <= capacity_ / 2)
        rehash_to(capacity_);
      else {
        if (capacity_ > SIZE_MAX / 2)
          throw std::bad_alloc();
        rehash_to(capacity_ * 2);
      }
    }
  }
  template <class K, class... A> Value *insert_new(K &&k, A &&...a) {
    entry pending(static_cast<K &&>(k), static_cast<A &&>(a)...);
    ensure_insert();
    auto code = uint64_t(hash_(pending.key_));
    auto i = vacant(code);
    if constexpr (detail::nothrow_move<Key> && detail::nothrow_move<Value>)
      ::new (static_cast<void *>(data_ + i))
          entry(static_cast<Key &&>(pending.key_),
                static_cast<Value &&>(pending.value_));
    else
      ::new (static_cast<void *>(data_ + i))
          entry(pending.key_, pending.value_);
    if (control_[i] == 2)
      --tombstones_;
    store_control(control_, capacity_, i, fingerprint(code));
    ++size_;
    return &data_[i].value_;
  }

public:
  basic_flat_hash_map() = default;
  explicit basic_flat_hash_map(const Allocator &a, const Hash &h = Hash{},
                               const Equal &e = Equal{})
      : alloc_(a), hash_(h), equal_(e) {}
  basic_flat_hash_map(const basic_flat_hash_map &o)
    requires(detail::copyable<Key> && detail::copyable<Value>)
      : alloc_(o.alloc_), hash_(o.hash_), equal_(o.equal_) {
    try {
      reserve(o.size_);
      for (const auto &e : o)
        try_emplace(e.key(), e.value());
    } catch (...) {
      clear();
      free_storage();
      throw;
    }
  }
  basic_flat_hash_map(basic_flat_hash_map &&o) noexcept
      : data_(o.data_), control_(o.control_), capacity_(o.capacity_),
        size_(o.size_), tombstones_(o.tombstones_), alloc_(o.alloc_),
        hash_(o.hash_), equal_(o.equal_) {
    o.data_ = nullptr;
    o.control_ = nullptr;
    o.capacity_ = o.size_ = o.tombstones_ = 0;
  }
  basic_flat_hash_map &operator=(const basic_flat_hash_map &o)
    requires(detail::copyable<Key> && detail::copyable<Value>)
  {
    if (this != &o) {
      basic_flat_hash_map copy(o);
      swap(copy);
    }
    return *this;
  }
  basic_flat_hash_map &operator=(basic_flat_hash_map &&o) noexcept {
    if (this != &o) {
      basic_flat_hash_map tmp(static_cast<basic_flat_hash_map &&>(o));
      swap(tmp);
    }
    return *this;
  }
  ~basic_flat_hash_map() {
    clear();
    free_storage();
  }
  void swap(basic_flat_hash_map &o) noexcept {
    auto d = data_;
    data_ = o.data_;
    o.data_ = d;
    auto c = control_;
    control_ = o.control_;
    o.control_ = c;
    auto n = size_;
    size_ = o.size_;
    o.size_ = n;
    auto cap = capacity_;
    capacity_ = o.capacity_;
    o.capacity_ = cap;
    auto t = tombstones_;
    tombstones_ = o.tombstones_;
    o.tombstones_ = t;
    auto a = alloc_;
    alloc_ = o.alloc_;
    o.alloc_ = a;
    auto h = hash_;
    hash_ = o.hash_;
    o.hash_ = h;
    auto e = equal_;
    equal_ = o.equal_;
    o.equal_ = e;
  }
  void reserve(size_t n) {
    if (n > SIZE_MAX / 2)
      throw std::bad_alloc();
    size_t cap = 8;
    while (cap < n * 2) {
      if (cap > SIZE_MAX / 2)
        throw std::bad_alloc();
      cap *= 2;
    }
    if (cap > capacity_)
      rehash_to(cap);
  }
  template <class K, class... A> Value *try_emplace(K &&k, A &&...a) {
    auto found = locate(k);
    if (found != SIZE_MAX)
      return &data_[found].value_;
    return insert_new(static_cast<K &&>(k), static_cast<A &&>(a)...);
  }
  template <class K, class V> void insert_or_assign(K &&k, V &&v) {
    auto i = locate(k);
    if (i != SIZE_MAX) {
      data_[i].value_ = static_cast<V &&>(v);
      return;
    }
    insert_new(static_cast<K &&>(k), static_cast<V &&>(v));
  }
  template <class Q> CE_FORCEINLINE Value *find(const Q &q) noexcept {
    auto i = locate(q);
    return i == SIZE_MAX ? nullptr : &data_[i].value_;
  }
  template <class Q>
  CE_FORCEINLINE const Value *find(const Q &q) const noexcept {
    auto i = locate(q);
    return i == SIZE_MAX ? nullptr : &data_[i].value_;
  }
  template <class Q> const entry *find_entry(const Q &q) const noexcept {
    auto i = locate(q);
    return i == SIZE_MAX ? nullptr : &data_[i];
  }
  template <class Q> CE_FORCEINLINE bool contains(const Q &q) const noexcept {
    return locate(q) != SIZE_MAX;
  }
  template <class Q> bool erase(const Q &q) noexcept {
    auto i = locate(q);
    if (i == SIZE_MAX)
      return false;
    data_[i].~entry();
    store_control(control_, capacity_, i, 2);
    --size_;
    ++tombstones_;
    if (!size_) {
      memset(control_, 0, capacity_ + 8);
      tombstones_ = 0;
    }
    return true;
  }
  void clear() noexcept {
    for (size_t i = 0; i < capacity_; ++i)
      if (control_[i] & 0x80u)
        data_[i].~entry();
    if (capacity_)
      memset(control_, 0, capacity_ + 8);
    size_ = tombstones_ = 0;
  }
  size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return !size_; }
  size_t capacity() const noexcept { return capacity_; }
  template <bool Const> class basic_iterator {
    using map_type = decltype([] {
      if constexpr (Const)
        return static_cast<const basic_flat_hash_map *>(nullptr);
      else
        return static_cast<basic_flat_hash_map *>(nullptr);
    }());
    map_type map_ = nullptr;
    size_t i_ = 0;
    void skip() {
      while (i_ < map_->capacity_ && !(map_->control_[i_] & 0x80u))
        ++i_;
    }

  public:
    basic_iterator(map_type m, size_t i) : map_(m), i_(i) { skip(); }
    decltype(auto) operator*() const {
      if constexpr (Const)
        return static_cast<const entry &>(map_->data_[i_]);
      else
        return static_cast<entry &>(map_->data_[i_]);
    }
    basic_iterator &operator++() {
      ++i_;
      skip();
      return *this;
    }
    bool operator==(const basic_iterator &o) const {
      return map_ == o.map_ && i_ == o.i_;
    }
  };
  using iterator = basic_iterator<false>;
  using const_iterator = basic_iterator<true>;
  iterator begin() { return {this, 0}; }
  iterator end() { return {this, capacity_}; }
  const_iterator begin() const { return {this, 0}; }
  const_iterator end() const { return {this, capacity_}; }
};
template <class V, class Allocator = heap_allocator>
using flat_hash_map = basic_flat_hash_map<uint64_t, V, hash<uint64_t>,
                                          equal_to<uint64_t>, Allocator>;
template <class V, class Allocator = heap_allocator>
using string_hash_map =
    basic_flat_hash_map<basic_string<Allocator>, V,
                        hash<basic_string<Allocator>>,
                        equal_to<basic_string<Allocator>>, Allocator>;
struct set_value {};
template <class Key, class Hash = hash<Key>, class Equal = equal_to<Key>,
          class Allocator = heap_allocator>
class flat_hash_set {
  basic_flat_hash_map<Key, set_value, Hash, Equal, Allocator> map_;

public:
  flat_hash_set() = default;
  explicit flat_hash_set(const Allocator &a) : map_(a) {}
  void reserve(size_t n) { map_.reserve(n); }
  template <class K> bool insert(K &&k) {
    if (map_.contains(k))
      return false;
    map_.try_emplace(static_cast<K &&>(k));
    return true;
  }
  template <class Q> bool erase(const Q &q) noexcept { return map_.erase(q); }
  template <class Q> bool contains(const Q &q) const noexcept {
    return map_.contains(q);
  }
  template <class Q> const Key *find(const Q &q) const noexcept {
    auto p = map_.find_entry(q);
    return p ? &p->key() : nullptr;
  }
  void clear() noexcept { map_.clear(); }
  size_t size() const noexcept { return map_.size(); }
  bool empty() const noexcept { return map_.empty(); }
  class const_iterator {
    typename decltype(map_)::const_iterator it_;

  public:
    explicit const_iterator(typename decltype(map_)::const_iterator i)
        : it_(i) {}
    const Key &operator*() const { return (*it_).key(); }
    const_iterator &operator++() {
      ++it_;
      return *this;
    }
    bool operator==(const const_iterator &o) const { return it_ == o.it_; }
  };
  const_iterator begin() const { return const_iterator(map_.begin()); }
  const_iterator end() const { return const_iterator(map_.end()); }
};
} // namespace ce
