#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.SpinLock.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#include <immintrin.h>


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

template<typename T>
class spin_lock
{
  public:
    spin_lock(T& lockObject) : lock_(lockObject) { lock(); }
    ~spin_lock() { unlock(); }

    void lock()
    {
        while (lock_.test_and_set(std::memory_order_acquire))
        {
            _mm_pause();
        }
    }

    void unlock() { lock_.clear(std::memory_order_release); }

  private:
    T& lock_;
};
