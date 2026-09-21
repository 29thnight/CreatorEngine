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

		void select_frame(std::uint32_t frame) { select_range(frame, frame); }
		void select_range(std::uint32_t first, std::uint32_t last);
		void select_latest();

		// 선택 구간의 집계. 선택이 바뀌지 않았으면 다시 접지 않는다.
		const frame_aggregate& aggregate() const;

		// 실제로 접은 횟수. 캐시가 도는지 밖에서 볼 수 있어야 검사가 된다 —
		// "느려지지 않았다" 는 말로만 적으면 아무도 재지 않는다.
		std::uint64_t fold_count() const { return m_foldCount; }

	private:
		void clamp_selection();

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
	};
}
