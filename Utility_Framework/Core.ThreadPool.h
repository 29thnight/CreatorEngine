#pragma once
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <windows.h>
#include <concurrent_queue.h>
#include "Core.Thread.h"
#include "Core.CountingSemaphore.h"

// WinAPI 기반 ThreadPool
// CountingSemaphore + 이벤트 기반 WaitAll() 대기 방식

template<typename TaskType = std::function<void()>>
class ThreadPool
{
private:
    using ConcurrentQueue = concurrency::concurrent_queue<TaskType>;
public:
    ThreadPool(int numThreads = 0, DWORD_PTR affinityMask = 0, int priority = THREAD_PRIORITY_NORMAL)
        : m_affinityMask(affinityMask), m_threadPriority(priority)
    {
        m_numThreads = (numThreads > 0) ? numThreads : static_cast<int>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        m_taskCounts.store(0);
        m_tasks = std::make_shared<ConcurrentQueue>();
        m_semaphore = std::make_shared<CountingSemaphore>(0);

        m_waitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr); // manual-reset, non-signaled

        m_threads.reserve(m_numThreads);
        for (int i = 0; i < m_numThreads; ++i)
        {
            auto thread = std::make_unique<Thread>();
            int threadIndex = i;
            thread->Start([this, threadIndex]() { this->WorkerLoop(threadIndex); });

            // Set affinity and priority
            if (m_affinityMask != 0)
                thread->SetAffinity((DWORD_PTR(1) << (threadIndex % 64)) & m_affinityMask);
            thread->SetPriority(m_threadPriority);

            m_threads.emplace_back(std::move(thread));
        }
    }

    ~ThreadPool()
    {
        m_exitFlag.store(true);

        m_semaphore->release(m_numThreads); // wake all threads

        for (auto& t : m_threads)
        {
            if (t)
                t->Join();
        }

        if (m_waitEvent)
        {
            CloseHandle(m_waitEvent);
            m_waitEvent = nullptr;
        }
    }

    template <class F>
    void Enqueue(F&& f)
    {
        m_taskCounts.fetch_add(1, std::memory_order_relaxed);
        ResetEvent(m_waitEvent);

        // TaskType로 단 한 번만 생성
        TaskType task(std::forward<F>(f));
        // rvalue 오버로드로 밀어넣기 (move)
        m_tasks->push(std::move(task));

        m_semaphore->release();
    }

    void NotifyAllAndWait()
    {
        if (m_taskCounts.load(std::memory_order_acquire) == 0)
            return;

        WaitForSingleObject(m_waitEvent, INFINITE);
    }

    int GetThreadCount() const { return m_numThreads; }

    void SetThreadInitCallback(std::function<void()> callback)
    {
        m_threadInitCallback = std::move(callback);
    }

    void SetThreadExitCallback(std::function<void()> callback)
    {
        m_threadExitCallback = std::move(callback);
	}

private:
    void WorkerLoop(int threadIndex)
    {
        if (m_threadInitCallback)
        {
            static thread_local bool s_initialized = [&]()
            {
                m_threadInitCallback();
                return true;
            }();
            (void)s_initialized;
        }

        while (!m_exitFlag.load(std::memory_order_acquire))
        {
            m_semaphore->acquire();

            if (m_exitFlag.load(std::memory_order_acquire))
                break;

            TaskType task;
            if (m_tasks->try_pop(task))
            {
                task();

                int remaining = m_taskCounts.fetch_sub(1, std::memory_order_release) - 1;
                if (remaining == 0)
                {
                    SetEvent(m_waitEvent); // 모든 작업 완료 시 이벤트 트리거
                }
            }
        }

        if (m_threadExitCallback)
        {
            m_threadExitCallback();
		}
    }

private:
    int m_numThreads = 0;
    std::vector<std::unique_ptr<Thread>> m_threads;

    std::atomic<bool> m_exitFlag{ false };
    std::atomic<int> m_taskCounts;

    std::shared_ptr<ConcurrentQueue> m_tasks;
    std::shared_ptr<CountingSemaphore> m_semaphore;

    HANDLE m_waitEvent = nullptr;
    DWORD_PTR m_affinityMask = 0;
    int m_threadPriority = THREAD_PRIORITY_NORMAL;

    std::function<void()> m_threadInitCallback;
	std::function<void()> m_threadExitCallback;
};
