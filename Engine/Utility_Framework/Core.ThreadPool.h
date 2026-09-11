#pragma once
#include <functional>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <windows.h>
#include <concurrent_queue.h>
#include "Core.Thread.h"
#include "Core.CountingSemaphore.h"

// WinAPI 기반 ThreadPool
// CountingSemaphore + 완료 카운터·조건변수 기반 WaitAll() 대기 방식
//
// ★ 배리어 규약: NotifyAllAndWait은 m_taskCounts가 0이 될 때까지 반환하지 않는다.
//   옛 manual-reset Event 판은 이것을 지키지 못했다. 워커의 SetEvent(카운터가
//   0이 된 뒤)와 생산자의 ResetEvent(0->1 전이를 본 뒤)가 엇갈리면 직전
//   배치의 늦은 신호가 새 배치의 리셋을 덮어, 잡이 남았는데도 대기가
//   즉시 풀렸다(독립 벤치 실측 2.5~5.8%, 배치당 태스크 8개 이상).
//   AnimationJob의 포즈 커밋 순서와 raw Animator* 수명이 이 불변식 위에 서 있다.
//   근거: docs/analysis/JobSystemFeasibilityAnalysis.md 10.1절

template<typename TaskType = std::function<void()>>
class ThreadPool
{
private:
    using ConcurrentQueue = concurrency::concurrent_queue<TaskType>;
public:
    ThreadPool(int numThreads = 0, DWORD_PTR affinityMask = 0, int priority = THREAD_PRIORITY_HIGHEST)
        : m_affinityMask(affinityMask), m_threadPriority(priority)
    {
        m_numThreads = (numThreads > 0) ? numThreads : static_cast<int>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        m_taskCounts.store(0);
        m_tasks = std::make_shared<ConcurrentQueue>();
        m_semaphore = std::make_shared<CountingSemaphore>(0);

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
    }

    template <class F>
    void Enqueue(F&& f)
    {
        m_taskCounts.fetch_add(1, std::memory_order_relaxed);

        TaskType task(std::forward<F>(f));
        m_tasks->push(std::move(task));

        m_semaphore->release();
    }

    void NotifyAllAndWait()
    {
        std::unique_lock<std::mutex> lock(m_doneMutex);
        m_doneCondition.wait(lock, [this]
        {
            return m_taskCounts.load(std::memory_order_acquire) == 0;
        });
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

                int remaining = m_taskCounts.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (remaining == 0)
                {
                    // 락을 잡고 통지한다. 대기자가 술어를 확인한 뒤 wait에
                    // 들어가기 전에 여기 도달해도 통지를 놓치지 않는다.
                    std::lock_guard<std::mutex> lock(m_doneMutex);
                    m_doneCondition.notify_all();
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

    std::mutex m_doneMutex;
    std::condition_variable m_doneCondition;

    DWORD_PTR m_affinityMask = 0;
    int m_threadPriority = THREAD_PRIORITY_NORMAL;

    std::function<void()> m_threadInitCallback;
	std::function<void()> m_threadExitCallback;
};
