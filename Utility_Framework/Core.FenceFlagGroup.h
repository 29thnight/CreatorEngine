#pragma once
#include <atomic>
#include <immintrin.h> // For _mm_pause

class FenceFlagGroup
{
public:
	explicit FenceFlagGroup(uint32_t count)
		: m_targetCount(count)
	{
		m_signaledFlags = new std::atomic_bool[count];
		for (uint32_t i = 0; i < count; ++i)
			m_signaledFlags[i].store(false, std::memory_order_relaxed);
	}

	~FenceFlagGroup()
	{
		delete[] m_signaledFlags;
	}

	// 스레드별로 고유 ID로 signal (index 0~count-1)
	void Signal(uint32_t threadIndex)
	{
		// 이미 true인 경우 다른 스레드가 Reset을 아직 하지 않은 상태일 수 있음
		while (m_signaledFlags[threadIndex].load(std::memory_order_acquire))
		{
			_mm_pause();
		}

		m_signaledFlags[threadIndex].store(true, std::memory_order_release);
	}

	void Wait()
	{
		while (true)
		{
			bool allReady = true;
			for (uint32_t i = 0; i < m_targetCount; ++i)
			{
				if (!m_signaledFlags[i].load(std::memory_order_acquire))
				{
					allReady = false;
					break;
				}
			}

			if (allReady)
				break;

			_mm_pause();
		}
	}

	void Reset()
	{
		for (uint32_t i = 0; i < m_targetCount; ++i)
			m_signaledFlags[i].store(false, std::memory_order_release);
	}

private:
	uint32_t m_targetCount;
	std::atomic_bool* m_signaledFlags;
};