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
		// �Ҹ� ���·� ��ȯ
		m_destroyed.store(true, std::memory_order_release);
		// �� ���̶� ��� ���� �����尡 �� �� �ֵ��� generation ����
		m_generation.fetch_add(1, std::memory_order_release);
	}

	// �����尡 ������ �� ȣ��
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
				// �� ���������� ���� ����� ��ȯ�ƴ���
				if (m_generation.load(std::memory_order_acquire) != gen)
					break;

				// �� ��ü�� �ı��Ǿ�����
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