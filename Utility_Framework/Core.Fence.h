#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>

/**
 * @class Fence
 * @brief A CPU-side synchronization primitive to coordinate work between threads.
 *
 * This class mimics the behavior of modern graphics API fences (like ID3D12Fence)
 * for CPU-CPU synchronization. It allows one thread to signal a value (typically
 * a frame number or job ID) and another thread to wait until that value is reached.
 * This is essential for managing the rendering pipeline where the main thread
 * produces rendering commands and the render thread consumes them.
 */
class Fence
{
public:
    Fence() : m_currentValue(0) {}
    ~Fence() = default;

    // Non-copyable and non-movable
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;
    Fence(Fence&&) = delete;
    Fence& operator=(Fence&&) = delete;

    /**
     * @brief Signals that a point has been reached by updating the fence value.
     * @param value The new value to set the fence to. Should be monotonically increasing.
     *
     * This is typically called by a producer thread (e.g., the render thread)
     * after it has completed a unit of work (like rendering a frame).
     */
    void Signal(uint64_t value)
    {
        // Atomically update the current value and notify any waiting threads.
        m_currentValue.store(value, std::memory_order_release);
        m_conditionVariable.notify_all();
    }

    /**
     * @brief Waits until the fence value reaches or exceeds the target value.
     * @param targetValue The value to wait for.
     *
     * This is called by a consumer thread (e.g., the main thread) to ensure that
     * the producer thread has reached a certain point before proceeding. This
     * prevents overwriting resources that are still in use by the GPU.
     */
    void Wait(uint64_t targetValue)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        // Wait until the predicate (lambda) returns true.
        // The predicate checks if the current value has reached the target.
        m_conditionVariable.wait(lock, [this, targetValue] {
            return m_currentValue.load(std::memory_order_acquire) >= targetValue;
            });
    }

    /**
     * @brief Gets the current value of the fence without waiting.
     * @return The last signaled value.
     */
    uint64_t GetValue() const
    {
        return m_currentValue.load(std::memory_order_acquire);
    }

private:
    // The current value of the fence. Atomically accessed.
    std::atomic<uint64_t> m_currentValue;

    // Mutex and condition variable for efficient waiting.
    std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
};