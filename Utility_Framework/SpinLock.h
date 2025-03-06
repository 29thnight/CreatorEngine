#pragma once
#include <atomic>
#include <immintrin.h> // For _mm_pause

template <typename T>
class SpinLock
{
public:
    SpinLock(T& lockObject) : m_lock(lockObject) { lock(); }
    ~SpinLock() { unlock(); }

    void lock()
    {
        while (m_lock.test_and_set(std::memory_order_acquire))
        {
            _mm_pause();
        }
    }

    void unlock()
    {
        m_lock.clear(std::memory_order_release);
    }

private:
    T& m_lock;
};
