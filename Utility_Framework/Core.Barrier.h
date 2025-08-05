#pragma once
#include <atomic>
#include <thread>
#include <immintrin.h> // for _mm_pause

class Barrier
{
public:
	explicit Barrier(int count)
		: m_threshold(count), m_count(count), m_generation(0)
	{
	}

	~Barrier()
	{
	}

	void Finalize()
	{
		// 소멸 상태로 전환
		m_destroyed.store(true, std::memory_order_release);
		// 한 번이라도 대기 중인 스레드가 깰 수 있도록 generation 증가
		m_generation.fetch_add(1, std::memory_order_release);
	}

	// 스레드가 도달할 때 호출
	void ArriveAndWait()
	{
		int gen = m_generation.load(std::memory_order_acquire);

		if (--m_count == 0)
		{
			m_count.store(m_threshold, std::memory_order_relaxed);
			m_generation.fetch_add(1, std::memory_order_release);
		}
		else
		{
			while (true)
			{
				// ① 정상적으로 다음 세대로 전환됐는지
				if (m_generation.load(std::memory_order_acquire) != gen)
					break;

				// ② 객체가 파괴되었는지
				if (m_destroyed.load(std::memory_order_acquire))
					break;

				_mm_pause();
			}
		}
	}

private:
	const int m_threshold;
	std::atomic<int> m_count;
	std::atomic<uint64_t> m_generation;
	std::atomic<bool> m_destroyed;
};

namespace BarrierHelper
{
	inline void RepeatArriveAndWait(Barrier& barrier, int count)
	{
		for (int i = 0; i < count; ++i)
			barrier.ArriveAndWait();
	}
}