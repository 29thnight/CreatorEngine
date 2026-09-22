#pragma once
// PHASE 14 P3 — 얼린 캡처를 읽는 집계.
//
// ★ 이 파일이 코어에 있는 이유.
//
//   P3 가 만드는 것(Frame Overview · Timeline · Hierarchy/Flat)은 전부 "얼린
//   캡처를 어떻게 접는가" 이고, 접는 일 자체에는 ImGui 가 한 줄도 필요 없다.
//   그리는 층에 두면 완료조건("Timeline 합계와 Hierarchy inclusive time 이
//   일치한다")을 잴 수단이 화면뿐이 된다 — P1·P2 에서 두 번 겪었듯 그리는
//   것만으로는 살았는지 알 수 없다.
//
//   그래서 접는 일은 여기 두고 검사는 코어 프로브가 한다. 코어가 표시를 모른다는
//   규약은 지켜진다 — 이 파일은 자료를 자료로 바꿀 뿐이다.
//
// ★ 트리를 어떻게 되살리는가.
//
//   이벤트는 **끝난 순서**로 기록된다(`end_scope` 에서 쓰므로 자식이 부모보다
//   먼저 들어간다). 그래서 목록을 그대로 읽으면 안쪽부터 나온다. 같은 스레드
//   안에서 (시작 tick 오름차순, depth 오름차순)으로 세우면 전위 순회가 되고,
//   그 뒤에는 depth 만으로 부모를 찾을 수 있다.
#include <cstdint>
#include <span>
#include <vector>

#include "ProfileCapture.h"

namespace ce
{
	// 집계 한 줄. Hierarchy 는 전위 순서로, Flat 은 total 내림차순으로 담긴다.
	struct aggregate_row
	{
		marker_id     marker = invalid_marker;
		std::uint16_t thread_slot = 0;
		std::uint16_t depth = 0;

		std::uint64_t call_count = 0;
		profile_tick  total_ticks = 0;   // inclusive — 자식을 포함한다
		profile_tick  self_ticks = 0;    // exclusive — 직속 자식의 total 을 뺀 것
		profile_tick  max_ticks = 0;     // 한 호출의 최대 inclusive
		profile_tick  min_ticks = 0;     // 한 호출의 최소 inclusive

		// 호출 길이의 95 백분위(§7.4 의 "긴 꼬리 확인").
		//
		// ★ nearest-rank 다 — 오름차순으로 세운 표본의 ceil(0.95 × n) 번째를
		//   **그대로** 낸다. 보간하지 않는 이유는, 보간한 수는 어느 호출도
		//   그만큼 걸린 적이 없는데 표에 서기 때문이다. 이 열을 보는 사람은
		//   "이만큼 걸린 호출이 있었다" 로 읽는다.
		profile_tick  p95_ticks = 0;

		// 이 marker 가 **나타난 프레임 수**(§7.4 의 Frames).
		//
		// ★ call_count 와 다르다. 한 프레임에 열 번 불린 것과 열 프레임에 한
		//   번씩 불린 것은 call_count 가 같고 이 수가 다르다 — 앞엣것은 그
		//   프레임 하나가 비싼 것이고 뒤엣것은 늘 켜져 있는 비용이다.
		std::uint32_t frame_appearances = 0;

		// 이 행에 **잘린 구간이 섞였다.** 녹화 시작을 못 본 채 끝났거나(begin)
		// 끝을 못 본 채 프레임이 넘어간(end) 구간이다. 표시하는 쪽은 이 줄의
		// 합계를 다른 줄과 나란히 두면 안 된다 — 길이가 실제보다 짧다.
		bool truncated = false;

		// 자식 행의 범위 [child_begin, child_end). Hierarchy 에서만 뜻이 있다.
		std::uint32_t child_begin = 0;
		std::uint32_t child_end = 0;
	};

	// 스레드 하나의 요약. Timeline 이 레인을 세우는 근거이고, 완료조건이
	// 말하는 "Timeline 합계" 가 여기의 root_ticks 다.
	struct thread_summary
	{
		std::uint16_t thread_slot = 0;
		std::uint32_t event_count = 0;
		std::uint16_t max_depth = 0;

		// depth 0 구간의 길이 합. 겹치지 않으므로 그냥 더한다.
		profile_tick  root_ticks = 0;

		// 이 스레드의 스팬이 spans() 안에서 차지하는 구간 [begin, end).
		// 레인을 그리는 쪽이 자기 몫만 훑을 수 있게 해 둔다 — 스팬이 스레드
		// 순으로 정렬돼 있으므로 연속이다.
		std::uint32_t span_begin = 0;
		std::uint32_t span_end = 0;
	};

	// 프레임 하나의 벽시계 구간(§7.3 의 첫째 트랙).
	//
	// ★ 집계가 들고 있는 tick_begin/tick_end 는 범위 **전체**의 양 끝이라
	//   프레임이 어디서 갈리는지는 말하지 못한다. 그리는 층이 캡처를 다시
	//   훑어 경계를 세면 접기가 두 곳에 생기고, 선택 범위가 바뀔 때 한쪽만
	//   따라간다.
	struct frame_boundary
	{
		std::uint32_t engine_frame = 0;
		profile_tick  tick_begin = 0;
		profile_tick  tick_end = 0;
	};

	// 프레임 범위 하나를 접은 결과. 만든 뒤에는 바뀌지 않는다.
	// 어디까지 접을 것인가.
	//
	// ★ Timeline 은 구간·레인·경계만 쓰고 Hierarchy/Flat 은 쳐다보지 않는다.
	//   그런데 이제 Timeline 이 **보이는 창 전체**(수백 프레임)를 그리고, 창은
	//   녹화 중에 계속 미끄러진다. 쓰지도 않는 두 표를 그때마다 같이 세우면
	//   프로파일러가 제가 재려는 프레임을 스스로 잡아먹는다.
	enum class aggregate_scope
	{
		full,        // 구간 + 레인 + Hierarchy + Flat
		spans_only,  // 구간 + 레인 + 경계 + 사건까지만
	};

	class frame_aggregate
	{
	public:
		std::span<const aggregate_row>  hierarchy() const { return m_hierarchy; }

		// ★ Timeline 이 그리는 원시 구간. 접기 전의 이벤트를 전위 순서
		//   (스레드 → 시작 tick → depth)로 세워 둔 것이다.
		//
		//   집계가 이미 이 순서로 세운 것을 **버리지 않고 남긴다**. 그리는
		//   층이 다시 정렬하면 같은 일을 두 번 하는 데다, 두 정렬이 갈리는
		//   순간 표와 타임라인이 서로 다른 트리를 말하게 된다.
		std::span<const profile_event>  spans() const { return m_spans; }
		std::span<const aggregate_row>  flat() const { return m_flat; }
		std::span<const thread_summary> threads() const { return m_threads; }

		// 이 범위에 든 프레임들의 경계. 엔진 프레임 오름차순이다.
		std::span<const frame_boundary> boundaries() const { return m_boundaries; }

		// 길이가 없는 사건(§7.3). spans() 안에도 있지만 시각으로 세워 두고
		// 따로 든다 — 경계 띠가 레인을 훑지 않고 이것만 그린다.
		std::span<const profile_event>  instants() const { return m_instants; }

		std::uint32_t frame_begin() const { return m_frameBegin; }
		std::uint32_t frame_end() const { return m_frameEnd; }   // [begin, end)
		std::uint32_t frame_count() const { return m_frameEnd - m_frameBegin; }
		std::uint64_t event_count() const { return m_eventCount; }
		std::uint64_t dropped_events() const { return m_droppedEvents; }
		std::uint64_t truncated_events() const { return m_truncatedEvents; }

		// 벽시계 구간 — 선택한 프레임들의 tick_begin 최소와 tick_end 최대.
		profile_tick  tick_begin() const { return m_tickBegin; }
		profile_tick  tick_end() const { return m_tickEnd; }

		// ★ 완료조건이 재는 값이다. 모든 스레드의 root_ticks 합과, Hierarchy
		//   루트 행들의 total_ticks 합. 같은 것을 두 길로 더하므로 **정확히**
		//   같아야 한다 — 부동소수로 바꾼 뒤 비교하면 어긋나도 오차로 읽힌다.
		profile_tick  timeline_total_ticks() const { return m_timelineTotal; }
		profile_tick  hierarchy_total_ticks() const { return m_hierarchyTotal; }

		friend frame_aggregate aggregate_frames(const capture_session&,
		                                        std::uint32_t, std::uint32_t,
		                                        aggregate_scope);

	private:
		std::vector<profile_event>  m_spans;
		std::vector<frame_boundary> m_boundaries;
		std::vector<profile_event>  m_instants;
		std::vector<aggregate_row>  m_hierarchy;
		std::vector<aggregate_row>  m_flat;
		std::vector<thread_summary> m_threads;

		std::uint32_t m_frameBegin = 0;
		std::uint32_t m_frameEnd = 0;
		std::uint64_t m_eventCount = 0;
		std::uint64_t m_droppedEvents = 0;
		std::uint64_t m_truncatedEvents = 0;
		profile_tick  m_tickBegin = 0;
		profile_tick  m_tickEnd = 0;
		profile_tick  m_timelineTotal = 0;
		profile_tick  m_hierarchyTotal = 0;
	};

	// 엔진 프레임 [first, last] 를 접는다(양끝 포함). 범위가 비었거나 캡처에
	// 없으면 빈 집계를 낸다 — 부르는 쪽이 판정할 수 있도록 던지지 않는다.
	frame_aggregate aggregate_frames(const capture_session& capture,
	                                 std::uint32_t first_frame,
	                                 std::uint32_t last_frame,
	                                 aggregate_scope scope = aggregate_scope::full);
}
