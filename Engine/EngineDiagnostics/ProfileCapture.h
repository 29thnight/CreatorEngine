#pragma once
// PHASE 14 P2 — rolling capture 와 immutable reader.
//
// 옛 코어는 5프레임 링에 덮어쓰며 UI 가 그 전역 vector 를 직접 읽었다. 그래서
// 스파이크를 발견했을 때 그 프레임은 이미 덮여 있었고("녹화" 가 아니라 4프레임
// 실시간이었다), 읽는 동안에도 기록이 계속돼 화면과 자료가 어긋났다.
//
// 여기서는 수집기가 봉인된 청크를 프레임 단위로 모으고, 얼리는 순간
// shared_ptr<const capture_session> 하나를 원자적으로 공개한다. reader 는
// 그 스냅샷만 보므로 엔진이 계속 돌아도 손에 든 자료가 변하지 않는다(§6.4).
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ProfileEvent.h"
#include "ProfileThreadStream.h"

namespace ce
{
	// 한 프레임에 수집된 것. 이벤트는 스레드가 섞여 있고 thread_slot 으로
	// 구분한다 — 옛 코어처럼 수집 시점에 스레드별로 정렬하지 않는다(그
	// 정렬의 부등호 하나가 스레드를 통째로 사라지게 했다).
	struct frame_record
	{
		std::uint32_t              engine_frame = 0;
		profile_tick               tick_begin = 0;
		profile_tick               tick_end = 0;
		std::vector<profile_event> events;

		std::uint64_t dropped_events = 0;

		std::size_t memory_bytes() const
		{
			return events.capacity() * sizeof(profile_event);
		}
	};

	// 기본값(§6.3). 60 FPS 에서 약 10초라는 UX 값일 뿐이고, 실제 보존 길이는
	// 이벤트 밀도에 따라 달라지므로 둘을 함께 표시한다.
	inline constexpr std::uint32_t kDefaultRetainedFrames = 600;
	inline constexpr std::size_t   kDefaultMemoryBudget = 128ull * 1024ull * 1024ull;

	// 얼어붙은 캡처. 생성 뒤에는 아무도 고치지 않는다.
	class capture_session
	{
	public:
		capture_session() = default;
		explicit capture_session(std::vector<frame_record> frames,
		                         std::vector<thread_info>  threads);

		std::span<const frame_record> frames() const { return m_frames; }
		std::span<const thread_info>  threads() const { return m_threads; }

		std::uint32_t frame_count() const { return static_cast<std::uint32_t>(m_frames.size()); }
		std::size_t   memory_bytes() const { return m_memoryBytes; }
		std::uint64_t total_events() const { return m_totalEvents; }

		const frame_record* find_frame(std::uint32_t engine_frame) const;

	private:
		std::vector<frame_record> m_frames;
		std::vector<thread_info>  m_threads;
		std::size_t               m_memoryBytes = 0;
		std::uint64_t             m_totalEvents = 0;
	};

	using capture_session_ptr = std::shared_ptr<const capture_session>;

	// 녹화 중 계속 쓰는 쪽. 수집 스레드(프레임 경계를 도는 쪽) 하나만 만진다.
	class capture_ring
	{
	public:
		void configure(std::uint32_t retained_frames, std::size_t memory_budget);
		void clear();

		// 봉인된 청크 목록을 프레임 버킷에 나눠 담는다. 청크는 호출자가
		// pool 에 되돌린다.
		void ingest(const event_chunk* sealed_list);

		// 이 프레임을 닫고 다음 프레임을 연다.
		void close_frame(std::uint32_t engine_frame, profile_tick tick_begin, profile_tick tick_end);

		// 이번 프레임에서 잃은 수와 누적을 함께 센다. 프레임 기록에는 이번
		// 프레임 몫만 들어가고, 누적은 링이 들고 있다.
		void note_dropped(std::uint64_t count)
		{
			m_pendingDropped += count;
			m_droppedEvents += count;
		}

		// 지금까지 모인 것을 얼려 공개한다. 링은 비우지 않는다 — 다시
		// 녹화를 눌러도 앞이 남아 있어야 하기 때문이다.
		capture_session_ptr freeze(std::span<const thread_info> threads) const;

		std::uint32_t retained_frames() const { return static_cast<std::uint32_t>(m_frames.size()); }
		std::size_t   memory_bytes() const { return m_memoryBytes; }
		std::uint64_t dropped_events() const { return m_droppedEvents; }

		// 가장 최근에 닫힌 프레임의 이벤트 수. 녹화 중에도 값싸게 읽히는
		// 요약이라 게이트와 HUD 가 이것을 본다(§6.4 의 live summary).
		std::uint32_t last_frame_events() const { return m_lastFrameEvents; }
		std::uint32_t peak_frame_events() const { return m_peakFrameEvents; }

		// 늦게 온 구간의 장부.
		//
		//   placed   제 프레임 칸을 찾아 들어간 것
		//   waiting  아직 그 프레임이 닫히지 않아 기다리는 것
		//   dropped  그 프레임이 이미 링 밖으로 밀려나 갈 곳이 없던 것
		//
		// ★ dropped 를 세지 않으면 "GPU 레인이 비었다" 와 "늦어서 잃었다" 가
		//   구분되지 않는다. 빈 집합을 성공으로 읽는 바로 그 양식이다.
		std::uint64_t late_spans_placed() const { return m_lateSpansPlaced; }
		std::uint64_t late_spans_dropped() const { return m_lateSpansDropped; }
		std::size_t   late_spans_waiting() const { return m_deferredSpans.size(); }

	private:
		void trim();

		// 이 구간을 제 프레임 칸에 넣는다. 그 프레임이 아직 안 닫혔으면
		// 기다리게 두고, 이미 밀려났으면 버리고 센다.
		void place_late_span(const profile_event& value);
		void drain_deferred_spans();

		std::vector<frame_record> m_frames;
		frame_record              m_pending;

		// 아직 제 프레임이 닫히지 않아 기다리는 구간. 상한을 두는 이유는
		// 프레임이 영영 안 닫히는 경우(녹화를 멈춘 채 GPU 만 도는 경우)에
		// 이 목록이 무한히 자라지 않게 하기 위해서다.
		static constexpr std::size_t kMaxDeferredSpans = 4096;
		std::vector<profile_event> m_deferredSpans;
		std::uint64_t m_lateSpansPlaced = 0;
		std::uint64_t m_lateSpansDropped = 0;

		std::uint32_t m_retainedFrames = kDefaultRetainedFrames;
		std::size_t   m_memoryBudget = kDefaultMemoryBudget;
		std::size_t   m_memoryBytes = 0;
		std::uint64_t m_droppedEvents = 0;
		std::uint64_t m_pendingDropped = 0;   // 아직 닫히지 않은 프레임의 몫
		std::uint32_t m_lastFrameEvents = 0;
		std::uint32_t m_peakFrameEvents = 0;
	};
}
