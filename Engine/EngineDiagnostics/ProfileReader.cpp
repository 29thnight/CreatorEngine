#include "ProfileReader.h"

#include <algorithm>

namespace ce
{
	bool capture_reader::sync(capture_session_ptr latest)
	{
		if (latest.get() == m_capture.get())
		{
			// 같은 것이면 손대지 않는다. 다시 adopt 하면 선택과 시야가 매 프레임
			// 초기화되고 접은 결과가 매 프레임 버려진다.
			return false;
		}

		if (!latest)
		{
			// 서비스가 내놓은 것이 없다고 해서 보던 것을 버리지는 않는다. 놓는 것은
			// Clear 가 reset() 으로 명시하는 일이다.
			return false;
		}

		if (m_capture && !m_liveFollow)
		{
			// ★ 따라가지 않기로 했으면 새로 얼린 것이 와도 보던 것을 지킨다.
			//   스파이크를 붙잡아 둔 손에서 빠져나가면 그 체크박스는 거짓말이 된다.
			return false;
		}

		adopt(std::move(latest));
		return true;
	}

	void capture_reader::adopt(capture_session_ptr capture)
	{
		m_capture = std::move(capture);
		m_aggregateValid = false;
		m_viewValid = false;

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
		m_viewValid = false;
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
			m_viewValid = false;
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

	// ── Timeline 의 가로 시야 ────────────────────────────────────────────────
	//
	// 최소 폭. 이 아래로 좁히면 스팬이 픽셀 하나에 뭉개지고, tick 이 정수라
	// 반올림이 시야를 뒤집을 수 있다(begin > end).
	namespace
	{
		constexpr profile_tick kMinimumViewTicks = 16;
	}

	void capture_reader::ensure_view() const
	{
		if (m_viewValid)
		{
			return;
		}

		const frame_aggregate& folded = aggregate();
		m_viewBegin = folded.tick_begin();
		m_viewEnd = folded.tick_end();
		if (m_viewEnd <= m_viewBegin)
		{
			m_viewEnd = m_viewBegin + kMinimumViewTicks;
		}
		m_viewValid = true;
	}

	void capture_reader::clamp_view() const
	{
		const frame_aggregate& folded = aggregate();
		const profile_tick low = folded.tick_begin();
		const profile_tick high = (folded.tick_end() > low)
			? folded.tick_end() : (low + kMinimumViewTicks);

		if (m_viewEnd <= m_viewBegin || (m_viewEnd - m_viewBegin) < kMinimumViewTicks)
		{
			m_viewEnd = m_viewBegin + kMinimumViewTicks;
		}

		profile_tick span = m_viewEnd - m_viewBegin;
		const profile_tick full = high - low;
		if (span > full)
		{
			span = full;
		}

		// 구간 밖으로 나가지 않는다. 나갈 수 있으면 빈 화면을 보게 되고,
		// 그때 사용자는 계측이 없다고 읽는다.
		if (m_viewBegin < low)
		{
			m_viewBegin = low;
		}
		if (m_viewBegin + span > high)
		{
			m_viewBegin = high - span;
		}
		m_viewEnd = m_viewBegin + span;
	}

	profile_tick capture_reader::view_begin() const
	{
		ensure_view();
		return m_viewBegin;
	}

	profile_tick capture_reader::view_end() const
	{
		ensure_view();
		return m_viewEnd;
	}

	profile_tick capture_reader::view_span() const
	{
		ensure_view();
		return (m_viewEnd > m_viewBegin) ? (m_viewEnd - m_viewBegin) : 0;
	}

	void capture_reader::reset_view()
	{
		m_viewValid = false;
	}

	void capture_reader::zoom_view(double factor, profile_tick pivot)
	{
		ensure_view();
		if (!(factor > 0.0))
		{
			return;
		}

		const profile_tick span = (m_viewEnd > m_viewBegin)
			? (m_viewEnd - m_viewBegin) : kMinimumViewTicks;

		// pivot 을 제자리에 두려면 그것이 시야에서 차지하는 비율을 지켜야 한다.
		const profile_tick clampedPivot = (pivot < m_viewBegin) ? m_viewBegin
			: ((pivot > m_viewEnd) ? m_viewEnd : pivot);
		const double ratio = static_cast<double>(clampedPivot - m_viewBegin)
			/ static_cast<double>(span);

		double scaled = static_cast<double>(span) * factor;
		if (scaled < static_cast<double>(kMinimumViewTicks))
		{
			scaled = static_cast<double>(kMinimumViewTicks);
		}
		const profile_tick nextSpan = static_cast<profile_tick>(scaled);

		const double nextBegin = static_cast<double>(clampedPivot)
			- ratio * static_cast<double>(nextSpan);
		m_viewBegin = (nextBegin > 0.0) ? static_cast<profile_tick>(nextBegin) : 0;
		m_viewEnd = m_viewBegin + nextSpan;
		clamp_view();
	}

	void capture_reader::pan_view(std::int64_t delta_ticks)
	{
		ensure_view();
		const profile_tick span = (m_viewEnd > m_viewBegin)
			? (m_viewEnd - m_viewBegin) : kMinimumViewTicks;

		if (delta_ticks < 0)
		{
			const profile_tick back = static_cast<profile_tick>(-delta_ticks);
			m_viewBegin = (m_viewBegin > back) ? (m_viewBegin - back) : 0;
		}
		else
		{
			m_viewBegin += static_cast<profile_tick>(delta_ticks);
		}
		m_viewEnd = m_viewBegin + span;
		clamp_view();
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
