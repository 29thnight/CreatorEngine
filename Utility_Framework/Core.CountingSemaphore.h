#pragma once
#include <windows.h>
#include <atomic>
#include <immintrin.h> // For _mm_pause

class CountingSemaphore
{
public:
	CountingSemaphore(int initialCount = 0)
		: m_count(initialCount)
	{
		m_semaphore = CreateSemaphoreW(nullptr, 0, LONG_MAX, nullptr);
	}

	~CountingSemaphore()
	{
		if (m_semaphore)
		{
			CloseHandle(m_semaphore);
			m_semaphore = nullptr;
		}
	}

	void acquire()
	{
		// 1. 유저 모드에서 스핀 대기
		for (int i = 0; i < SpinCount; ++i)
		{
			int expected = m_count.load(std::memory_order_relaxed);
			if (expected > 0 && m_count.compare_exchange_weak(expected, expected - 1, std::memory_order_acquire))
				return;

			_mm_pause(); // 스핀
		}

		// 2. 커널 모드 대기
		if (m_count.fetch_sub(1, std::memory_order_acquire) <= 0)
		{
			WaitForSingleObject(m_semaphore, INFINITE);
		}
	}

	void release(int count = 1)
	{
		int prev = m_count.fetch_add(count, std::memory_order_release);

		// 블로킹 중인 스레드가 있을 경우에만 세마포어 해제
		if (prev < 0)
		{
			ReleaseSemaphore(m_semaphore, count, nullptr);
		}
	}

private:
	static constexpr int SpinCount = 1024;

	std::atomic<int> m_count;
	HANDLE m_semaphore;
};
