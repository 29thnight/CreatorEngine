#pragma once
#include <atomic>
#include <thread>
#include <immintrin.h> // for _mm_pause

class Barrier
{
public:
	explicit Barrier(int count) :
		m_threshold(count),
		m_count(count),
		m_generation(0),
		m_destroyed(false),
		m_breakRequested(false)
	{
	}

	~Barrier()
	{
	}

	void Finalize()
	{
		// Destroy the barrier and release waiting threads.
		m_destroyed.store(true, std::memory_order_release);
		// Advance the generation to wake up threads.
		m_generation.fetch_add(1, std::memory_order_release);
	}

	void BreakBegin()
	{
		bool expected = false;
		if (m_breakRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
		{
			m_count.store(m_threshold, std::memory_order_release);
			m_generation.fetch_add(1, std::memory_order_release);
		}
	}

	void BreakEnd()
	{
		m_breakRequested.store(false, std::memory_order_release);
	}

	// 스레드가 도달할 때 호출
	void ArriveAndWait()
	{
		if (m_breakRequested.load(std::memory_order_acquire))
		{
			return;
		}

		const int generation = m_generation.load(std::memory_order_acquire);

		if (m_breakRequested.load(std::memory_order_acquire))
		{
			return;
		}

		const int previousCount = m_count.fetch_sub(1, std::memory_order_acq_rel);
		if (previousCount == 1)
		{
			m_count.store(m_threshold, std::memory_order_release);
			m_generation.fetch_add(1, std::memory_order_release);
			return;
		}

		while (true)
		{
			if (m_breakRequested.load(std::memory_order_acquire))
				break;

			if (m_generation.load(std::memory_order_acquire) != generation)
				break;

			if (m_destroyed.load(std::memory_order_acquire))
				break;

			_mm_pause();
		}
	}

private:
	const int m_threshold;
	std::atomic<int> m_count;
	std::atomic<uint64_t> m_generation;
	std::atomic<bool> m_destroyed;
	std::atomic<bool> m_breakRequested;
};

namespace BarrierHelper
{
	inline void RepeatArriveAndWait(Barrier& barrier, int count)
	{
		for (int i = 0; i < count; ++i)
			barrier.ArriveAndWait();
	}
}