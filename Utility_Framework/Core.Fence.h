#pragma once
#include <atomic>
#include <thread>
#include <Windows.h>

class Fence
{
public:
    Fence()
    {
        m_fenceValue.store(0);
        m_targetValue = 0;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    ~Fence()
    {
        CloseHandle(m_event);
    }

    // Signal 예약: 이 함수는 Fence를 활성화합니다.
    void Begin()
    {
        m_targetValue = m_fenceValue.load(std::memory_order_acquire) + 1;
    }

    // Fence에 도달했다고 Signal함 (보통 다른 스레드에서 호출)
    void Signal()
    {
        m_fenceValue.fetch_add(1, std::memory_order_release);

        // 값이 도달했으면 이벤트 발생
        if (m_fenceValue.load() >= m_targetValue)
            SetEvent(m_event);
    }

    // Fence 완료까지 기다림 (블로킹)
    void Wait()
    {
        if (IsComplete())
            return;

        WaitForSingleObject(m_event, INFINITE);
    }

    // Fence 완료 여부 확인 (비차단)
    bool IsComplete() const
    {
        return m_fenceValue.load(std::memory_order_acquire) >= m_targetValue;
    }

private:
    std::atomic<uint64_t> m_fenceValue;
    uint64_t m_targetValue;
    HANDLE m_event;
};
