#pragma once
#include "atomic.h"
namespace ce::detail {
// Sequentially consistent pointer operations, used for hazard publication too.
template <class T> class atomic_ptr {
#if !defined(_MSC_VER)
  static_assert(__atomic_always_lock_free(sizeof(T *), nullptr),
                "Pointer atomics must not fall back to an internal lock");
#endif
#if defined(_MSC_VER)
  T *volatile value_ = nullptr;
#else
  T *value_ = nullptr;
#endif
public:
  atomic_ptr() noexcept = default;
  explicit atomic_ptr(T *p) noexcept : value_(p) {}
  atomic_ptr(const atomic_ptr &) = delete;
  atomic_ptr &operator=(const atomic_ptr &) = delete;
  T *load() const noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    auto bits = __iso_volatile_load64(
        reinterpret_cast<const volatile __int64 *>(&value_));
    _ReadWriteBarrier();
    return reinterpret_cast<T *>(bits);
#elif defined(_MSC_VER)
    return static_cast<T *>(_InterlockedCompareExchangePointer(
        reinterpret_cast<void *volatile *>(const_cast<T *volatile *>(&value_)),
        nullptr, nullptr));
#else
    return __atomic_load_n(&value_, __ATOMIC_SEQ_CST);
#endif
  }
  void store(T *p) noexcept { (void)exchange(p); }
  T *exchange(T *p) noexcept {
#if defined(_MSC_VER)
    return static_cast<T *>(_InterlockedExchangePointer(
        reinterpret_cast<void *volatile *>(&value_), p));
#else
    return __atomic_exchange_n(&value_, p, __ATOMIC_SEQ_CST);
#endif
  }
  bool compare_exchange(T *&expected, T *desired) noexcept {
#if defined(_MSC_VER)
    auto old = static_cast<T *>(_InterlockedCompareExchangePointer(
        reinterpret_cast<void *volatile *>(&value_), desired, expected));
    if (old == expected)
      return true;
    expected = old;
    return false;
#else
    return __atomic_compare_exchange_n(&value_, &expected, desired, false,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
  }
};

} // namespace ce::detail
