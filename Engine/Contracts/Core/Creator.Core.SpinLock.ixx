module;
#include <immintrin.h>

export module Creator.Core.SpinLock;

import std;

export template<typename T>
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
