#pragma once
#include <stdint.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
namespace ce::detail {
// Compiler atomics only: no <atomic>, PPL, Windows SDK, mutex, or STL
// dependency. x86 MSVC uses aligned intrinsic loads/stores + compiler barriers;
// other MSVC targets conservatively use full interlocked barriers.
class atomic_u32 {
#if defined(_MSC_VER)
  mutable volatile long value_ = 0;
#else
  mutable uint32_t value_ = 0;
#endif
public:
  atomic_u32() noexcept = default;
  atomic_u32(const atomic_u32 &) = delete;
  atomic_u32 &operator=(const atomic_u32 &) = delete;
  uint32_t load() const noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    auto x =
        __iso_volatile_load32(reinterpret_cast<const volatile int *>(&value_));
    _ReadWriteBarrier();
    return static_cast<uint32_t>(x);
#elif defined(_MSC_VER)
    return static_cast<uint32_t>(_InterlockedCompareExchange(&value_, 0, 0));
#else
    return __atomic_load_n(&value_, __ATOMIC_ACQUIRE);
#endif
  }
  void store(uint32_t x) noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _ReadWriteBarrier();
    __iso_volatile_store32(reinterpret_cast<volatile int *>(&value_),
                           static_cast<int>(x));
#elif defined(_MSC_VER)
    _InterlockedExchange(&value_, static_cast<long>(x));
#else
    __atomic_store_n(&value_, x, __ATOMIC_RELEASE);
#endif
  }
  bool compare_exchange(uint32_t &expected, uint32_t desired) noexcept {
#if defined(_MSC_VER)
    auto old = static_cast<uint32_t>(_InterlockedCompareExchange(
        &value_, static_cast<long>(desired), static_cast<long>(expected)));
    if (old == expected)
      return true;
    expected = old;
    return false;
#else
    return __atomic_compare_exchange_n(&value_, &expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
  }
  uint32_t fetch_add(uint32_t x) noexcept {
#if defined(_MSC_VER)
    return static_cast<uint32_t>(
        _InterlockedExchangeAdd(&value_, static_cast<long>(x)));
#else
    return __atomic_fetch_add(&value_, x, __ATOMIC_SEQ_CST);
#endif
  }
  uint32_t exchange(uint32_t x) noexcept {
#if defined(_MSC_VER)
    return static_cast<uint32_t>(
        _InterlockedExchange(&value_, static_cast<long>(x)));
#else
    return __atomic_exchange_n(&value_, x, __ATOMIC_ACQ_REL);
#endif
  }
};
inline void spin_pause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
  __yield();
#elif defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#else
  __atomic_signal_fence(__ATOMIC_SEQ_CST);
#endif
}
class spin_mutex {
  atomic_u32 held_;

public:
  void lock() noexcept {
    unsigned delay = 1;
    for (;;) {
      if (held_.exchange(1) == 0)
        return;
      do {
        for (unsigned i = 0; i < delay; ++i)
          spin_pause();
        if (delay < 64)
          delay *= 2;
      } while (held_.load() != 0);
    }
  }
  void unlock() noexcept { held_.store(0); }
};
class lock_guard {
  spin_mutex &mutex_;

public:
  explicit lock_guard(spin_mutex &m) noexcept : mutex_(m) { m.lock(); }
  ~lock_guard() { mutex_.unlock(); }
  lock_guard(const lock_guard &) = delete;
  lock_guard &operator=(const lock_guard &) = delete;
};
} // namespace ce::detail
