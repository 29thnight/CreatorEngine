#pragma once
#include <atomic>
#include <immintrin.h> // For _mm_pause

class FenceFlagGroup
{
public:
	explicit FenceFlagGroup(uint32_t count)
		: m_targetCount(count), m_currentCount(0)
	{
	}

	// �� �����尡 �Ϸ�Ǿ��� �� ȣ��
	void Signal()
	{
		uint32_t old = m_currentCount.fetch_add(1, std::memory_order_acq_rel);
		// optional assert
		// assert(old < m_targetCount);
	}

	// ��� �����尡 Signal() ȣ���� ������ ���
	void Wait()
	{
		while (m_currentCount.load(std::memory_order_acquire) < m_targetCount)
		{
			_mm_pause(); // or std::this_thread::yield();
		}
	}

	// �ٽ� ����� �� �ֵ��� �ʱ�ȭ
	void Reset(uint32_t newTargetCount = 0)
	{
		m_currentCount.store(0, std::memory_order_release);
		if (newTargetCount != 0)
			m_targetCount = newTargetCount;
	}

private:
	std::atomic<uint32_t> m_currentCount;
	uint32_t m_targetCount;
};