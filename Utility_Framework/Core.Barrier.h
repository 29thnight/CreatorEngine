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

	// �����尡 ������ �� ȣ��
	void ArriveAndWait()
	{
		int gen = m_generation.load(std::memory_order_acquire);

		if (--m_count == 0)
		{
			// ������ ������: ī���� �ʱ�ȭ �� ���� ��ȯ
			m_count.store(m_threshold, std::memory_order_relaxed);
			m_generation.fetch_add(1, std::memory_order_release);
		}
		else
		{
			// �ٸ� ������: ���� ������� ���
			while (m_generation.load(std::memory_order_acquire) == gen)
			{
				_mm_pause(); // spin wait
			}
		}
	}

private:
	const int m_threshold;
	std::atomic<int> m_count;
	std::atomic<uint64_t> m_generation;
};

namespace BarrierHelper
{
	inline void RepeatArriveAndWait(Barrier& barrier, int count)
	{
		for (int i = 0; i < count; ++i)
			barrier.ArriveAndWait();
	}
}