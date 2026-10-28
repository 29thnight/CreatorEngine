#pragma once
#include "atomic.h"
namespace ce::detail
{
    class atomic_u64
    {
#if defined(_MSC_VER)
        alignas(8) mutable volatile __int64 value_ = 0;
#else
        static_assert(__atomic_always_lock_free(8, nullptr));
        alignas(8) uint64_t value_ = 0;
#endif
      public:
        atomic_u64() noexcept = default;
        atomic_u64(const atomic_u64&) = delete;
        atomic_u64& operator=(const atomic_u64&) = delete;

        uint64_t load() const noexcept
        {
#if defined(_MSC_VER) && defined(_M_X64)
            auto v = __iso_volatile_load64(&value_);
            _ReadWriteBarrier();
            return static_cast<uint64_t>(v);
#elif defined(_MSC_VER)
            return static_cast<uint64_t>(_InterlockedCompareExchange64(&value_, 0, 0));
#else
            return __atomic_load_n(&value_, __ATOMIC_SEQ_CST);
#endif
        }

        uint64_t fetch_add(uint64_t n) noexcept
        {
#if defined(_MSC_VER)
            return static_cast<uint64_t>(_InterlockedExchangeAdd64(&value_, static_cast<__int64>(n)));
#else
            return __atomic_fetch_add(&value_, n, __ATOMIC_SEQ_CST);
#endif
        }

        bool compare_exchange(uint64_t& expected, uint64_t desired) noexcept
        {
#if defined(_MSC_VER)
            auto old = static_cast<uint64_t>(
                _InterlockedCompareExchange64(&value_, static_cast<__int64>(desired), static_cast<__int64>(expected)));
            if (old == expected)
                return true;
            expected = old;
            return false;
#else
            return __atomic_compare_exchange_n(&value_, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
        }
    };
} // namespace ce::detail
