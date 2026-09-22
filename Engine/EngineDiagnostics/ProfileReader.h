#pragma once
// PHASE 14 P3 — reader 상태.
//
// §6.4 가 "UI selection 과 정렬은 reader 쪽 별도 상태" 라고 적은 그 상태다.
// 그리는 층에 두지 않는 이유는 집계와 같다 — 완료조건 둘("과거 프레임 선택
// 가능", "pause 후 엔진이 계속 돌아도 선택 자료가 변하지 않음")이 순수 상태라
// 화면 없이 잴 수 있기 때문이다.
//
// ★ 녹화 중에는 볼 것이 없다는 것이 설계다(§6.4). recording 동안 공개되는
//   것은 경량 `live_summary` 뿐이고, 링을 직접 읽는 reader 는 없다 — 읽는
//   동안 기록이 계속돼 화면과 자료가 어긋나던 것이 옛 코어의 결함이었다.
//   그래서 이 reader 는 `pause()` 가 공개한 얼린 캡처만 받는다.
//
// ★ Live Follow 는 "새 캡처가 오면 선택을 최신으로 옮긴다" 는 뜻이다. 끄면
//   보고 있던 프레임을 지킨다 — 스파이크를 붙잡아 두는 것이 그 토글의 쓸모다.
#include <cstdint>
#include <memory>

#include "ProfileAggregate.h"

namespace ce
{
	class capture_reader
	{
	public:
		// 얼린 캡처를 받는다. live_follow 면 선택이 최신 프레임으로 가고,
		// 아니면 보던 선택을 지킨다(캡처 범위 밖이면 범위 안으로 자른다 —
		// rolling ring 이 오래된 프레임을 버리면 선택이 밖으로 나간다).
		void adopt(capture_session_ptr capture);

		// 서비스가 내놓은 최신 얼린 캡처를 따라간다. 창이 매 프레임 부른다.
		// 갈아타면 true.
		//
		// ★ 관문을 녹화 **상태**가 아니라 손에 든 것으로 건다. 얼린 순간은
		//   보는 쪽이 한 번도 못 볼 수 있는 찰나다 — pause 와 record 가 한 프레임
		//   사이에 다 지나가면 "frozen 일 때 집는다" 는 관문은 영영 열리지 않고,
		//   그러면 캡처가 있는데도 빈 화면을 본다.
		//
		// ★ 이 정책이 창이 아니라 여기 있는 이유는 **재기 위해서**다. 화면에
		//   두면 갈아타는지를 물을 수단이 눈뿐이 된다.
		bool sync(capture_session_ptr latest);

		// 캡처를 놓는다. Clear 가 부른다.
		void reset();

		bool has_capture() const { return static_cast<bool>(m_capture); }
		const capture_session* capture() const { return m_capture.get(); }

		bool live_follow() const { return m_liveFollow; }
		void set_live_follow(bool value);

		// 캡처가 들고 있는 프레임 범위 [first, last]. 없으면 둘 다 0 이고
		// has_capture() 가 false 다.
		std::uint32_t available_first() const { return m_availableFirst; }
		std::uint32_t available_last() const { return m_availableLast; }

		std::uint32_t selected_first() const { return m_selectedFirst; }
		std::uint32_t selected_last() const { return m_selectedLast; }
		std::uint32_t selected_count() const { return m_selectedLast - m_selectedFirst + 1; }

		// ── 프레임 그래프의 창(§7.2) ────────────────────────────────────
		//
		// ★ 보존된 프레임을 **전부 한 화면에 뭉개지 않는다.** 막대 폭은
		//   고정이고, 화면에 담기는 만큼만 보여 준다. 새 프레임은 오른쪽에서
		//   들어오고 옛 프레임은 왼쪽으로 밀려 나간다 — 폭을 프레임 수로
		//   나누면 같은 프레임이 매 스냅샷 다른 자리에 그려져서, 그래프가
		//   흐르는 것이 아니라 매번 새로 그려지는 그림이 된다.
		//
		// ★ 이 상태가 reader 에 있는 이유는 선택·시야와 같다. 화면에 두면
		//   "끝까지 가면 멈춘다" 같은 계약을 잴 수단이 눈뿐이 된다.
		std::uint32_t graph_first() const;
		std::uint32_t graph_count() const;
		std::uint32_t graph_last() const;

		// 화면이 몇 프레임을 담을 수 있는지 그리는 쪽이 알려 준다. 따라가는
		// 중이면 오른쪽 끝을 최신에 붙인 채 왼쪽으로 늘어난다.
		void set_graph_span(std::uint32_t frames);

		// 창을 옮긴다. 보존 구간 밖으로는 나가지 않는다. 뒤로 굴리면 따라가기가
		// 풀리고, 오른쪽 끝에 닿으면 다시 켜진다.
		//
		// ★ 고른 프레임도 **같이 움직인다.** 굴리는 것은 "지금 보고 있는
		//   자리" 하나여야 한다 — 그래프만 밀리고 아래 타임라인이 제자리면
		//   축이 둘이 되고, 그때 사람은 두 곳을 따로 맞춰야 한다.
		void pan_graph(std::int32_t delta_frames);

		// 보존 구간 전체로 되돌린다.
		void reset_graph();

		void select_frame(std::uint32_t frame) { select_range(frame, frame); }
		void select_range(std::uint32_t first, std::uint32_t last);
		void select_latest();

		// 선택 구간의 집계. 선택이 바뀌지 않았으면 다시 접지 않는다.
		// Hierarchy·Flat 표가 이것을 읽는다.
		const frame_aggregate& aggregate() const;

		// ── 보이는 창의 집계 ────────────────────────────────────────────
		//
		// ★ Timeline 이 읽는 것은 **이쪽**이다. 고른 한 프레임만 그리면
		//   위 그래프가 244 프레임을 보여 주는 동안 아래는 1.6 ms 짜리 한 칸만
		//   그린다 — 같은 화면의 두 그림이 서로 다른 범위를 말한다.
		//
		//   구간·레인·경계까지만 접는다(`spans_only`). 창은 녹화 중에 계속
		//   미끄러지고, 그때마다 쓰지도 않는 표를 세우면 프로파일러가 제가
		//   재려는 프레임을 잡아먹는다.
		const frame_aggregate& window_aggregate() const;

		// ── Timeline 의 가로 시야 ───────────────────────────────────────────
		//
		// 선택한 프레임들의 벽시계 구간 안에서 어디를 보고 있는가. 확대·이동이
		// 여기 있는 이유는 선택과 같다 — 순수 상태라 화면 없이 잴 수 있고,
		// "확대해도 구간 밖으로 나가지 않는다" 같은 계약을 프로브가 문다.
		//
		// ★ 기준은 **보이는 창**이다(선택이 아니다). 그래서 다른 프레임을
		//   골라도 시야는 그대로다 — 창 안에 있는 한 빈 화면이 될 수가 없고,
		//   고를 때마다 확대가 풀리면 확대가 뜻이 없다. 창이 미끄러지면,
		//   확대해 두지 않았을 때만 따라간다.
		profile_tick view_begin() const;
		profile_tick view_end() const;
		profile_tick view_span() const;

		void reset_view();

		// pivot 을 제자리에 두고 배율을 바꾼다. factor < 1 이면 확대(구간이
		// 좁아진다), > 1 이면 축소. 선택 구간보다 넓어지지 않고, 최소 폭
		// 아래로 좁아지지도 않는다.
		void zoom_view(double factor, profile_tick pivot);

		// 시야를 옮긴다. 선택 구간 밖으로는 나가지 않는다 — 나갈 수 있으면
		// 빈 화면을 보게 되고, 그때 사용자는 계측이 없다고 읽는다.
		void pan_view(std::int64_t delta_ticks);

		// 실제로 접은 횟수. 캐시가 도는지 밖에서 볼 수 있어야 검사가 된다 —
		// "느려지지 않았다" 는 말로만 적으면 아무도 재지 않는다.
		std::uint64_t fold_count() const { return m_foldCount; }

	private:
		void clamp_selection();
		void shift_selection(std::int64_t delta);
		void clamp_view() const;
		void ensure_view() const;
		void rebase_view_to_window() const;

		capture_session_ptr m_capture;
		bool          m_liveFollow = true;

		std::uint32_t m_availableFirst = 0;
		std::uint32_t m_availableLast = 0;
		std::uint32_t m_selectedFirst = 0;
		std::uint32_t m_selectedLast = 0;

		// 캐시. 접는 일은 const 함수 안에서 일어나므로 mutable 이다 —
		// 부르는 쪽에서 보면 reader 는 읽기 전용이다.
		mutable frame_aggregate m_aggregate;
		mutable bool            m_aggregateValid = false;
		mutable std::uint64_t   m_foldCount = 0;

		// 시야는 집계가 서야 뜻이 생긴다(구간의 tick 을 알아야 한다). 그래서
		// 처음 물을 때 세우고, 창이 바뀌면 무효가 된다.
		mutable profile_tick m_viewBegin = 0;
		mutable profile_tick m_viewEnd = 0;
		mutable bool         m_viewValid = false;

		// 창 집계의 캐시. 접은 범위를 같이 들고 있어야 창이 미끄러진 것을 안다.
		mutable frame_aggregate m_windowAggregate;
		mutable std::uint32_t   m_windowAggregateFirst = 0;
		mutable std::uint32_t   m_windowAggregateLast = 0;
		mutable bool            m_windowAggregateValid = false;

		// 시야가 창 전체를 덮고 있는가. 덮고 있으면 창이 미끄러질 때 따라가고,
		// 확대해 둔 상태면 보던 자리를 지킨다.
		mutable bool m_viewSpansWholeWindow = true;

		// 그래프의 창. 0 이면 "아직 세우지 않았다" 이고, 그때는 보존 구간
		// 전체를 뜻한다.
		void clamp_graph();
		void clamp_graph_range();
		std::uint32_t m_graphFirst = 0;
		std::uint32_t m_graphCount = 0;

		// 한 화면에 이보다 적게 보여 주지 않는다. 더 좁히면 막대 몇 개만
		// 남아 그래프가 아니라 점이 된다.
		static constexpr std::uint32_t kMinimumGraphFrames = 8;
	};
}
