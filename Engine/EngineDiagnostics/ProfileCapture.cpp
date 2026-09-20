#include "ProfileCapture.h"

#include <algorithm>
#include <utility>

namespace ce
{
	//-------------------------------------------------------------------------
	// capture_session
	//-------------------------------------------------------------------------

	capture_session::capture_session(std::vector<frame_record> frames,
	                                 std::vector<thread_info>  threads)
		: m_frames(std::move(frames))
		, m_threads(std::move(threads))
	{
		for (const frame_record& frame : m_frames)
		{
			m_memoryBytes += frame.memory_bytes();
			m_totalEvents += frame.events.size();
		}
	}

	const frame_record* capture_session::find_frame(std::uint32_t engine_frame) const
	{
		// 프레임은 증가 순으로 들어오므로 이분 탐색이 성립한다.
		const auto found = std::lower_bound(
			m_frames.begin(), m_frames.end(), engine_frame,
			[](const frame_record& record, std::uint32_t value) { return record.engine_frame < value; });

		if (found == m_frames.end() || found->engine_frame != engine_frame)
		{
			return nullptr;
		}
		return &(*found);
	}

	//-------------------------------------------------------------------------
	// capture_ring
	//-------------------------------------------------------------------------

	void capture_ring::configure(std::uint32_t retained_frames, std::size_t memory_budget)
	{
		m_retainedFrames = retained_frames > 0 ? retained_frames : kDefaultRetainedFrames;
		m_memoryBudget = memory_budget > 0 ? memory_budget : kDefaultMemoryBudget;
	}

	void capture_ring::clear()
	{
		m_frames.clear();
		m_pending = frame_record{};
		m_memoryBytes = 0;
		m_droppedEvents = 0;
		m_lastFrameEvents = 0;
		m_peakFrameEvents = 0;
	}

	void capture_ring::ingest(const event_chunk* sealed_list)
	{
		while (sealed_list)
		{
			// 청크 안의 순서는 writer 가 지켰다. 여기서는 그대로 잇는다 —
			// 수집 시점에 정렬하지 않는 것이 이 설계의 요점이다.
			m_pending.events.insert(
				m_pending.events.end(),
				sealed_list->events,
				sealed_list->events + sealed_list->count);

			sealed_list = sealed_list->next;
		}
	}

	void capture_ring::close_frame(std::uint32_t engine_frame, profile_tick tick_begin, profile_tick tick_end)
	{
		m_pending.engine_frame = engine_frame;
		m_pending.tick_begin = tick_begin;
		m_pending.tick_end = tick_end;

		// 프레임이 가져가는 것은 **이 프레임의** 드롭이다. 누적을 넣으면
		// 프레임마다 같은 큰 수가 박혀 어느 프레임이 실제로 잃었는지
		// 구분되지 않는다. 누적은 m_droppedEvents 가 따로 들고 있다.
		m_pending.dropped_events = m_pendingDropped;
		m_pendingDropped = 0;

		m_lastFrameEvents = static_cast<std::uint32_t>(m_pending.events.size());
		m_peakFrameEvents = std::max(m_peakFrameEvents, m_lastFrameEvents);

		m_memoryBytes += m_pending.memory_bytes();
		m_frames.push_back(std::move(m_pending));
		m_pending = frame_record{};

		trim();
	}

	void capture_ring::trim()
	{
		// 프레임 수와 메모리 예산 중 먼저 닿는 쪽을 따른다. 가장 오래된
		// **완결된** 프레임부터 버리므로 지금 쓰는 프레임은 건드리지 않는다.
		//
		// 앞에서 하나씩 erase 하면 매번 뒤를 전부 옮겨 O(n^2) 이 된다.
		// 몇 개를 버릴지 먼저 정하고 한 번에 옮긴다.
		std::size_t drop = 0;
		if (m_frames.size() > m_retainedFrames)
		{
			drop = m_frames.size() - m_retainedFrames;
		}

		std::size_t bytes = m_memoryBytes;
		for (std::size_t i = 0; i < drop; ++i)
		{
			bytes -= m_frames[i].memory_bytes();
		}

		// 예산을 넘으면 더 버린다. 마지막 한 프레임은 남긴다 — 예산이 한
		// 프레임보다 작게 잡혀도 캡처가 통째로 비어 버리지 않게.
		while (bytes > m_memoryBudget && drop + 1 < m_frames.size())
		{
			bytes -= m_frames[drop].memory_bytes();
			++drop;
		}

		if (drop == 0)
		{
			return;
		}

		m_memoryBytes = bytes;
		m_frames.erase(m_frames.begin(), m_frames.begin() + static_cast<std::ptrdiff_t>(drop));
	}

	capture_session_ptr capture_ring::freeze(std::span<const thread_info> threads) const
	{
		// 복사해서 넘긴다. 이 복사가 reader 를 recorder 에서 떼어 내는 값이고,
		// 얼린 뒤 엔진이 계속 돌아도 손에 든 자료가 변하지 않는 이유다.
		std::vector<frame_record> frames(m_frames.begin(), m_frames.end());
		std::vector<thread_info>  thread_list(threads.begin(), threads.end());
		return std::make_shared<const capture_session>(std::move(frames), std::move(thread_list));
	}
}
