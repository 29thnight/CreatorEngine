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

    // Signal ����: �� �Լ��� Fence�� Ȱ��ȭ�մϴ�.
    void Begin()
    {
        m_targetValue = m_fenceValue.load(std::memory_order_acquire) + 1;
    }

    // Fence�� �����ߴٰ� Signal�� (���� �ٸ� �����忡�� ȣ��)
    void Signal()
    {
        m_fenceValue.fetch_add(1, std::memory_order_release);

        // ���� ���������� �̺�Ʈ �߻�
        if (m_fenceValue.load() >= m_targetValue)
            SetEvent(m_event);
    }

    // Fence �Ϸ���� ��ٸ� (���ŷ)
    void Wait()
    {
        if (IsComplete())
            return;

        WaitForSingleObject(m_event, INFINITE);
    }

    // Fence �Ϸ� ���� Ȯ�� (������)
    bool IsComplete() const
    {
        return m_fenceValue.load(std::memory_order_acquire) >= m_targetValue;
    }

private:
    std::atomic<uint64_t> m_fenceValue;
    uint64_t m_targetValue;
    HANDLE m_event;
};
