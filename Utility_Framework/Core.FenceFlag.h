#pragma once
#include <atomic>
#include <immintrin.h>

class FenceFlag
{
public:
	FenceFlag() : m_signaled(false) {}

	void Signal()
	{
		m_signaled.store(true, std::memory_order_release);
	}

	void Wait()
	{
		while (!m_signaled.load(std::memory_order_acquire))
		{
			_mm_pause();
		}
		m_signaled.store(false, std::memory_order_release);
	}

private:
	std::atomic<bool> m_signaled;
};