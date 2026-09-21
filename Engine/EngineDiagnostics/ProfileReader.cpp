#include "ProfileReader.h"

#include <algorithm>

namespace ce
{
	void capture_reader::adopt(capture_session_ptr capture)
	{
		m_capture = std::move(capture);
		m_aggregateValid = false;

		if (!m_capture || m_capture->frame_count() == 0)
		{
			m_capture.reset();
			m_availableFirst = m_availableLast = 0;
			m_selectedFirst = m_selectedLast = 0;
			return;
		}

		const std::span<const frame_record> frames = m_capture->frames();
		m_availableFirst = frames.front().engine_frame;
		m_availableLast = frames.back().engine_frame;

		if (m_liveFollow)
		{
			m_selectedFirst = m_selectedLast = m_availableLast;
			return;
		}

		// ★ 따라가지 않기로 했으면 보던 자리를 지킨다. 다만 rolling ring 이
		//   그 프레임을 버렸으면 지킬 수가 없으므로 범위 안으로 자른다 —
		//   조용히 최신으로 점프하면 "붙잡아 뒀다" 는 약속이 깨진다.
		clamp_selection();
	}

	void capture_reader::reset()
	{
		m_capture.reset();
		m_availableFirst = m_availableLast = 0;
		m_selectedFirst = m_selectedLast = 0;
		m_aggregateValid = false;
	}

	void capture_reader::set_live_follow(bool value)
	{
		if (m_liveFollow == value)
		{
			return;
		}
		m_liveFollow = value;

		// 켜는 순간 최신으로 간다. 켜 두고 아무 일도 안 일어나면 토글이
		// 다음 캡처까지 아무 뜻도 없어 보인다.
		if (m_liveFollow && m_capture)
		{
			select_latest();
		}
	}

	void capture_reader::select_range(std::uint32_t first, std::uint32_t last)
	{
		if (last < first)
		{
			std::swap(first, last);
		}

		const std::uint32_t previousFirst = m_selectedFirst;
		const std::uint32_t previousLast = m_selectedLast;

		m_selectedFirst = first;
		m_selectedLast = last;
		clamp_selection();

		if (m_selectedFirst != previousFirst || m_selectedLast != previousLast)
		{
			m_aggregateValid = false;
		}
	}

	void capture_reader::select_latest()
	{
		if (!m_capture)
		{
			return;
		}
		select_range(m_availableLast, m_availableLast);
	}

	void capture_reader::clamp_selection()
	{
		if (!m_capture)
		{
			m_selectedFirst = m_selectedLast = 0;
			return;
		}

		m_selectedFirst = std::clamp(m_selectedFirst, m_availableFirst, m_availableLast);
		m_selectedLast = std::clamp(m_selectedLast, m_availableFirst, m_availableLast);
		if (m_selectedLast < m_selectedFirst)
		{
			m_selectedLast = m_selectedFirst;
		}
	}

	const frame_aggregate& capture_reader::aggregate() const
	{
		if (m_aggregateValid)
		{
			return m_aggregate;
		}

		if (m_capture)
		{
			m_aggregate = aggregate_frames(*m_capture, m_selectedFirst, m_selectedLast);
			++m_foldCount;
		}
		else
		{
			m_aggregate = frame_aggregate{};
		}
		m_aggregateValid = true;
		return m_aggregate;
	}
}
