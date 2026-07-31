#pragma once
#include <atomic>
#include <thread>
#include <immintrin.h> // for _mm_pause
#include <profileapi.h>

// 게임/렌더 스레드 락스텝 동기화 (PHASE 3-2에서 해체 대상).
//
// 대기 시간을 재 둔다. 락스텝을 푸는 일은 크고 위험한 변경이라, "얼마나 손해인가"를
// 모르고 시작하면 이득 없는 위험만 떠안을 수 있다. 여기서 나온 숫자가 곧 해체의
// 근거이자, 해체 후 비교할 기준선이다.
//
// 계측 비용은 스핀에 실제로 들어간 경우에만 QPC 두 번이다(먼저 도착해 바로
// 통과하는 경우는 재지 않는다). 프레임당 스레드 3 x 랑데뷰 2 수준이라 무시할 만하다.
struct BarrierStats
{
	uint64_t arrivals{ 0 };     // 배리어에 도달한 횟수
	uint64_t spins{ 0 };        // 그중 실제로 기다린 횟수
	double   waitMilliseconds{ 0.0 };
};

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
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		m_ticksPerSecond = static_cast<double>(frequency.QuadPart);
	}

	BarrierStats GetStats() const
	{
		BarrierStats stats{};
		stats.arrivals = m_arrivals.load(std::memory_order_relaxed);
		stats.spins = m_spins.load(std::memory_order_relaxed);
		stats.waitMilliseconds = (m_ticksPerSecond > 0.0)
			? (static_cast<double>(m_waitTicks.load(std::memory_order_relaxed)) / m_ticksPerSecond) * 1000.0
			: 0.0;
		return stats;
	}

	void ResetStats()
	{
		m_arrivals.store(0, std::memory_order_relaxed);
		m_spins.store(0, std::memory_order_relaxed);
		m_waitTicks.store(0, std::memory_order_relaxed);
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

	// 스레드가 도달했을 때 호출한다. 마지막 도착자가 세대를 올려 나머지를 깨운다.
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

		m_arrivals.fetch_add(1, std::memory_order_relaxed);

		const int previousCount = m_count.fetch_sub(1, std::memory_order_acq_rel);
		if (previousCount == 1)
		{
			// 마지막 도착자는 기다리지 않는다 — 여기는 재지 않는다.
			m_count.store(m_threshold, std::memory_order_release);
			m_generation.fetch_add(1, std::memory_order_release);
			return;
		}

		LARGE_INTEGER begin{};
		QueryPerformanceCounter(&begin);

		// 양보하지 않고 스핀한다.
		//
		// 대기가 프레임당 수 ms 규모(실측: 세 스레드 합산 6.1ms, 프레임 6.7ms)라
		// 코어를 붙들고 있는 게 아깝게 보여서, 512회 스핀 뒤 std::this_thread::yield()로
		// 양보하는 방식을 재 봤다. 결과는 더 나빴다 — 같은 600프레임 구간에서
		// 벽시계 17.99s → 19.02s(+5.7%), 배리어 대기 6.13 → 7.46ms/프레임(+22%).
		// 양보한 스레드가 다시 스케줄되기까지의 지연이 절약한 코어 시간보다 컸다.
		// 코어가 넉넉한 장비라 스핀이 남을 굶기지 않는 것으로 보인다.
		//
		// 다른 장비에서 다시 판단하려면 render.syncstats로 재 보고 A/B 할 것.
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

		LARGE_INTEGER end{};
		QueryPerformanceCounter(&end);

		m_spins.fetch_add(1, std::memory_order_relaxed);
		m_waitTicks.fetch_add(static_cast<uint64_t>(end.QuadPart - begin.QuadPart),
			std::memory_order_relaxed);
	}

private:
	const int m_threshold;
	std::atomic<int> m_count;
	std::atomic<uint64_t> m_generation;
	std::atomic<bool> m_destroyed;
	std::atomic<bool> m_breakRequested;

	// 계측. relaxed로만 다뤄서 동기화 의미에 끼어들지 않게 한다.
	std::atomic<uint64_t> m_arrivals{ 0 };
	std::atomic<uint64_t> m_spins{ 0 };
	std::atomic<uint64_t> m_waitTicks{ 0 };
	double m_ticksPerSecond{ 0.0 };
};

namespace BarrierHelper
{
	inline void RepeatArriveAndWait(Barrier& barrier, int count)
	{
		for (int i = 0; i < count; ++i)
			barrier.ArriveAndWait();
	}
}