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
		m_windowAggregateValid = false;
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

			// ★ 따라가는 동안에는 그래프도 **끝을 붙잡는다.** 창 너비는
			//   그대로 두고 오른쪽 끝만 최신에 맞춘다 — 확대해 둔 것이
			//   새 스냅샷마다 풀리면 확대가 뜻이 없다.
			if (0 != m_graphCount)
			{
				const std::uint32_t span = m_graphCount;
				m_graphFirst = (m_availableLast + 1 > span)
					? (m_availableLast + 1 - span) : m_availableFirst;
			}
			clamp_graph();
			return;
		}

		// ★ 따라가지 않기로 했으면 보던 자리를 지킨다. 다만 rolling ring 이
		//   그 프레임을 버렸으면 지킬 수가 없으므로 범위 안으로 자른다 —
		//   조용히 최신으로 점프하면 "붙잡아 뒀다" 는 약속이 깨진다.
		clamp_selection();

		// ★ 그래프도 같다. 보던 구간이 링에서 밀려나면 자르되, 최신으로
		//   점프시키지는 않는다 — 뒤로 굴려 놓고 보던 사람의 손에서 자료가
		//   빠져나가는 것이 이 창이 있는 까닭과 정반대다.
		clamp_graph();
	}

	// ── 프레임 그래프의 창(§7.2) ────────────────────────────────────────
	//
	// 0 은 "아직 세우지 않았다" 이고 보존 구간 전체를 뜻한다. 캡처가 없으면
	// 둘 다 0 이라 그리는 쪽이 아무것도 안 그린다.
	std::uint32_t capture_reader::graph_first() const
	{
		if (!m_capture) return 0;
		return (0 == m_graphCount) ? m_availableFirst : m_graphFirst;
	}

	std::uint32_t capture_reader::graph_count() const
	{
		if (!m_capture) return 0;
		const std::uint32_t available = m_availableLast - m_availableFirst + 1;
		return (0 == m_graphCount) ? available : m_graphCount;
	}

	std::uint32_t capture_reader::graph_last() const
	{
		const std::uint32_t count = graph_count();
		return (0 == count) ? 0 : (graph_first() + count - 1);
	}

	void capture_reader::set_graph_span(std::uint32_t frames)
	{
		if (!m_capture || 0 == frames) return;

		const std::uint32_t anchor = graph_last();
		m_graphCount = frames;

		// ★ 폭이 바뀔 때 움직이는 것은 **왼쪽 끝**이다. 오른쪽 끝을 붙잡아야
		//   창을 넓혀도 보고 있던 최신 프레임이 제자리에 남는다.
		m_graphFirst = (anchor + 1 > frames) ? (anchor + 1 - frames) : m_availableFirst;
		clamp_graph();
	}

	void capture_reader::pan_graph(std::int32_t delta_frames)
	{
		if (!m_capture || 0 == delta_frames) return;

		const std::uint32_t before = graph_first();
		const std::uint32_t count = graph_count();
		const std::int64_t first = static_cast<std::int64_t>(before) + delta_frames;

		m_graphCount = count;
		m_graphFirst = (first < static_cast<std::int64_t>(m_availableFirst))
			? m_availableFirst : static_cast<std::uint32_t>(first);
		clamp_graph();

		// ★ 고른 프레임을 **같은 만큼** 민다. 굴리는 것은 "지금 보고 있는
		//   자리" 하나여야 한다 — 그래프 창만 움직이고 선택이 제자리면 아래
		//   타임라인은 그대로라서, 스크롤이 위쪽 그림만 흔드는 것처럼 보인다.
		//
		//   경계에서 창이 덜 움직였으면 선택도 덜 움직여야 한다. 그래서 청한
		//   양이 아니라 **실제로 움직인 양**을 쓴다.
		const std::int64_t applied =
			static_cast<std::int64_t>(m_graphFirst) - static_cast<std::int64_t>(before);
		shift_selection(applied);

		// ★ 뒤로 굴렸으면 따라가기를 끈다. 안 끄면 다음 스냅샷이 창을 최신으로
		//   되돌려서, 손으로 굴린 것이 한 프레임 만에 사라진다.
		const std::uint32_t lastFirst = (m_availableLast + 1 > m_graphCount)
			? (m_availableLast + 1 - m_graphCount) : m_availableFirst;
		set_live_follow(m_graphFirst >= lastFirst);
	}

	// 고른 구간을 통째로 옮긴다. 폭은 지킨다 — 굴리다가 선택이 넓어지거나
	// 좁아지면 아래 표의 수가 조용히 달라진다.
	void capture_reader::shift_selection(std::int64_t delta)
	{
		if (0 == delta) return;

		const std::int64_t first = static_cast<std::int64_t>(m_selectedFirst) + delta;
		const std::int64_t last = static_cast<std::int64_t>(m_selectedLast) + delta;
		const std::int64_t floor = static_cast<std::int64_t>(m_availableFirst);

		m_selectedFirst = static_cast<std::uint32_t>((std::max)(first, floor));
		m_selectedLast = static_cast<std::uint32_t>((std::max)(last, floor));
		clamp_selection();
	}

	void capture_reader::reset_graph()
	{
		m_graphFirst = 0;
		m_graphCount = 0;
	}

	void capture_reader::clamp_graph()
	{
		clamp_graph_range();

		// ★ 창이 움직였으면 시야를 다시 앉힌다. 안 하면 타임라인이 지난 창의
		//   tick 을 그대로 들고 있어서, 굴린 뒤 빈 화면이 나온다.
		if (m_windowAggregateValid
		    && (m_windowAggregateFirst != graph_first()
		        || m_windowAggregateLast != graph_last()))
		{
			rebase_view_to_window();
		}
	}

	void capture_reader::clamp_graph_range()
	{
		if (!m_capture || 0 == m_graphCount)
		{
			return;
		}

		const std::uint32_t available = m_availableLast - m_availableFirst + 1;
		if (m_graphCount > available)
		{
			m_graphCount = available;
		}
		if (m_graphCount < kMinimumGraphFrames)
		{
			m_graphCount = (available < kMinimumGraphFrames) ? available : kMinimumGraphFrames;
		}

		if (m_graphFirst < m_availableFirst)
		{
			m_graphFirst = m_availableFirst;
		}

		// ★ 오른쪽 끝을 넘지 않는다. 넘으면 빈 칸을 그리게 되고, 그때
		//   사용자는 "그 프레임들이 사라졌다" 로 읽는다.
		const std::uint32_t lastFirst = m_availableLast - m_graphCount + 1;
		if (m_graphFirst > lastFirst)
		{
			m_graphFirst = lastFirst;
		}
	}

	void capture_reader::reset()
	{
		m_capture.reset();
		m_availableFirst = m_availableLast = 0;
		m_selectedFirst = m_selectedLast = 0;
		m_aggregateValid = false;
		m_windowAggregateValid = false;
		m_viewValid = false;
		reset_graph();
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
			// ★ 시야는 건드리지 않는다. 기준이 선택에서 창으로 옮겨졌으므로
			//   다른 프레임을 골라도 타임라인이 그리는 범위는 그대로고,
			//   여기서 되돌리면 확대해 둔 것이 클릭 한 번에 풀린다.
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

	// ── Timeline 의 가로 시야 ────────────────────────────────────────────────
	//
	// 최소 폭. 이 아래로 좁히면 스팬이 픽셀 하나에 뭉개지고, tick 이 정수라
	// 반올림이 시야를 뒤집을 수 있다(begin > end).
	namespace
	{
		constexpr profile_tick kMinimumViewTicks = 16;
	}

	// ★ 기준이 선택에서 **창**으로 옮겨졌다. 고른 한 프레임을 기준으로 삼으면
	//   위 그래프가 244 프레임을 보여 주는 동안 아래 타임라인은 1.6 ms 짜리
	//   한 칸만 그린다.
	void capture_reader::ensure_view() const
	{
		if (m_viewValid)
		{
			return;
		}

		const frame_aggregate& folded = window_aggregate();
		m_viewBegin = folded.tick_begin();
		m_viewEnd = folded.tick_end();
		if (m_viewEnd <= m_viewBegin)
		{
			m_viewEnd = m_viewBegin + kMinimumViewTicks;
		}
		m_viewValid = true;
		m_viewSpansWholeWindow = true;
	}

	// 창이 미끄러졌을 때 시야를 어떻게 할 것인가.
	//
	// ★ 확대해 두지 않았으면 **따라간다.** 확대해 뒀으면 보던 자리를 지키고
	//   범위 안으로만 자른다 — 굴릴 때마다 확대가 풀리면 확대가 뜻이 없다.
	void capture_reader::rebase_view_to_window() const
	{
		if (!m_viewValid)
		{
			return;
		}

		if (m_viewSpansWholeWindow)
		{
			m_viewValid = false;
			return;
		}
		clamp_view();
	}

	void capture_reader::clamp_view() const
	{
		const frame_aggregate& folded = window_aggregate();
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

		// 확대해 뒀는가. 창이 미끄러질 때 따라갈지 자리를 지킬지가 여기서
		// 갈린다 — 그래서 자르는 자리에서 한 번만 적는다.
		m_viewSpansWholeWindow = (span >= full);
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

	const frame_aggregate& capture_reader::window_aggregate() const
	{
		const std::uint32_t first = graph_first();
		const std::uint32_t last = graph_last();

		if (m_windowAggregateValid
		    && m_windowAggregateFirst == first && m_windowAggregateLast == last)
		{
			return m_windowAggregate;
		}

		if (m_capture)
		{
			m_windowAggregate =
				aggregate_frames(*m_capture, first, last, aggregate_scope::spans_only);
			++m_foldCount;
		}
		else
		{
			m_windowAggregate = frame_aggregate{};
		}
		m_windowAggregateFirst = first;
		m_windowAggregateLast = last;
		m_windowAggregateValid = true;
		return m_windowAggregate;
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
