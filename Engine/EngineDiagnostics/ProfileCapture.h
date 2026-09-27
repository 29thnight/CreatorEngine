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
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ProfileEvent.h"
#include "ProfileThreadStream.h"

namespace ce
{
	// P5 counters are sparse: an absent sample means unavailable, never zero.
	// IDs are stable in .ceprof; add descriptors before publishing new metrics.
	enum class profile_counter_id : std::uint16_t
	{
		process_cpu_percent = 1,
		process_ram_mb = 2,
		gpu_vram_mb = 3,
		lan_send_bytes_per_second = 4,
		lan_receive_bytes_per_second = 5,
		upload_bytes = 6,
		upload_overflows = 7,
		descriptor_allocations = 8,
		descriptor_overflows = 9,
		draw_calls = 10,
		batches = 11,
		gc_gen0_collections = 12,
		gc_gen1_collections = 13,
		gc_gen2_collections = 14,
		gc_heap_mb = 15,
		gc_fragmented_mb = 16,
		gc_pause_percent = 17,
		resource_models = 18,
		resource_materials = 19,
		resource_textures = 20,
		resource_proxies = 21,
		provider_process_us = 22,
		provider_render_us = 23,
		provider_gc_us = 24,
		provider_resources_us = 25,
	};

	// Descriptors are copied into each capture. Runtime providers may register
	// more IDs; a file reader never resolves them against this process's registry.
	enum class counter_category : std::uint32_t
	{
		process = 1u << 0, gpu = 1u << 1, network = 1u << 2,
		render = 1u << 3, managed = 1u << 4, resources = 1u << 5,
	};
	using counter_mask = std::uint32_t;
	constexpr counter_mask counter_bit(counter_category category)
	{
		return static_cast<counter_mask>(category);
	}
	struct capture_counter
	{
		profile_counter_id id{};
		std::string name;
		std::string unit;
		counter_category category = counter_category::process;
	};
	profile_counter_id register_counter(std::string_view name, std::string_view unit,
	                                    counter_category category);
	std::vector<capture_counter> snapshot_counters();
	counter_mask counter_category_bit(profile_counter_id id);
	const capture_counter* find_counter(std::span<const capture_counter> counters,
	                                    profile_counter_id id);
	struct profile_counter_sample
	{
		profile_counter_id id{};
		double value = 0.0;
	};
	// 봉인된 producer 페이지를 그대로 참조한다. 프레임을 얼릴 때는 페이지
	// 참조와 작은 인덱스만 복사한다. 파일에서 읽은 이벤트는 owned 구간에
	// 담고, 늦게 들어온 이벤트도 가능한 한 원본 페이지의 구간을 참조한다.
	class frame_events
	{
	public:
		class const_iterator
		{
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = profile_event;
			using difference_type = std::ptrdiff_t;
			using pointer = const profile_event*;
			using reference = const profile_event&;
			const_iterator() = default;
			const_iterator(const frame_events* owner, std::size_t index)
				: m_owner(owner), m_index(index) {}
			reference operator*() const { return (*m_owner)[m_index]; }
			pointer operator->() const { return &(**this); }
			const_iterator& operator++() { ++m_index; return *this; }
			const_iterator operator++(int) { auto copy = *this; ++*this; return copy; }
			bool operator==(const const_iterator& other) const
				{ return m_owner == other.m_owner && m_index == other.m_index; }
		private:
			const frame_events* m_owner = nullptr;
			std::size_t m_index = 0;
		};

		std::size_t size() const { return m_count; }
		bool empty() const { return m_count == 0; }
		const_iterator begin() const { return { this, 0 }; }
		const_iterator end() const { return { this, m_count }; }
		const profile_event& operator[](std::size_t index) const;
		profile_event& mutable_at(std::size_t index);
		void push_back(const profile_event& value);
		void resize(std::size_t count);
		void append_page(std::shared_ptr<const event_chunk> page,
		                 std::uint32_t offset, std::uint32_t count);
		std::size_t memory_bytes() const { return m_memoryBytes; }

	private:
		struct segment
		{
			std::shared_ptr<const event_chunk> page;
			std::shared_ptr<std::vector<profile_event>> owned;
			std::size_t begin = 0;
			std::uint32_t offset = 0;
			std::uint32_t count = 0;
		};
		void materialize();
		std::vector<segment> m_segments;
		std::size_t m_count = 0;
		std::size_t m_memoryBytes = 0;
	};

	// 한 프레임에 수집된 것. 이벤트는 스레드가 섞여 있고 thread_slot 으로
	// 구분한다 — 옛 코어처럼 수집 시점에 스레드별로 정렬하지 않는다(그
	// 정렬의 부등호 하나가 스레드를 통째로 사라지게 했다).
	struct frame_record
	{
		std::uint32_t              engine_frame = 0;
		profile_tick               tick_begin = 0;
		profile_tick               tick_end = 0;
		frame_events events;
		std::vector<profile_counter_sample> counters;

		std::uint64_t dropped_events = 0;

		std::size_t memory_bytes() const
		{
			return events.memory_bytes() + counters.capacity() * sizeof(profile_counter_sample);
		}
	};

	// 기본값(§6.3). 60 FPS 에서 약 10초라는 UX 값일 뿐이고, 실제 보존 길이는
	// 이벤트 밀도에 따라 달라지므로 둘을 함께 표시한다.
	inline constexpr std::uint32_t kDefaultRetainedFrames = 600;
	inline constexpr std::size_t   kDefaultMemoryBudget = 128ull * 1024ull * 1024ull;

	// 캡처가 **자기 뜻을 풀기 위해** 들고 다녀야 하는 것. 전부 뜬 기계의
	// 값이고, 읽는 기계의 것으로 풀면 숫자가 조용히 틀린다(§8.2).
	//
	// ★ 인자를 늘리는 대신 묶었다. `freeze(threads, complete, unacked,
	//   ticks_per_second)` 는 같은 폭의 정수가 줄줄이 서서, 자리를 바꿔 넣어도
	//   컴파일러가 아무 말도 하지 않는다.
	struct capture_environment
	{
		// tick → 초. 이 값으로 나눠야 ms 가 나온다.
		//
		// ★ 지금까지 환산은 **읽는 기계의** QPC 주파수로 했다. 라이브에서는
		//   같은 기계라 맞았지만, 파일에서 읽은 캡처에서는 모든 구간 길이가
		//   두 주파수의 비만큼 틀린다 — 그리고 화면에는 그럴듯한 숫자가
		//   그대로 나오므로 눈으로도 게이트로도 못 잡는다.
		profile_tick ticks_per_second = 0;
	};

	// 얼어붙은 캡처. 생성 뒤에는 아무도 고치지 않는다.
	class capture_session
	{
	public:
		capture_session() = default;
		capture_session(std::vector<frame_record>   frames,
		                std::vector<thread_info>    threads,
		                std::vector<capture_marker> markers,
		                capture_environment         environment,
		                bool                        complete,
		                std::uint32_t               unacked_streams,
		                std::uint64_t               dropped_counters = 0,
		                std::vector<capture_counter> counters = {});

		std::span<const frame_record> frames() const { return m_frames; }
		std::span<const thread_info>  threads() const { return m_threads; }

		// ── 이 캡처의 어휘(P6) ───────────────────────────────────────────
		//
		// ★ 이름을 푸는 자리는 **여기 하나**다. `ce::marker_info(id)` 로 풀면
		//   전역 registry 를 읽게 되고, 파일에서 온 캡처에서는 그것이 남의
		//   이름이다. 읽는 쪽은 언제나 자기가 들고 있는 캡처에 물어라.
		std::span<const capture_marker> markers() const { return m_markers; }
		std::span<const capture_counter> counter_descriptors() const { return m_counters; }

		// ★ inline 으로 두지 않는다. 변이 하네스는 **컴파일 목록의 파일**만
		//   갈아 끼울 수 있어서, 헤더에 적힌 계약은 이빨을 증명할 수단이 없다.
		std::uint32_t marker_count() const;

		// 모르는 id 는 빈 이름을 돌려준다 — 던지지도, 전역으로 새지도 않는다.
		const capture_marker& marker(marker_id id) const;

		// ── 이 캡처의 시계(P6) ───────────────────────────────────────────
		const capture_environment& environment() const { return m_environment; }

		// tick 을 ms 로. ★ **이 캡처의** 주파수로 나눈다. 주파수를 모르면
		// 0 을 낸다 — 이 기계의 것으로 대신 나누지 않는다. 틀린 숫자보다
		// 빈 숫자가 낫다.
		double milliseconds(profile_tick ticks) const;

		std::uint32_t frame_count() const { return static_cast<std::uint32_t>(m_frames.size()); }
		std::size_t   memory_bytes() const { return m_memoryBytes; }
		std::uint64_t total_events() const { return m_totalEvents; }
		std::uint64_t dropped_counters() const { return m_droppedCounters; }

		// 얼릴 때 **모든** 스트림이 봉인에 응답했는가. false 면 이 캡처에는
		// 어느 스레드의 꼬리가 빠져 있다.
		//
		// ★ 세는 것과 판정하는 것은 다르다. 미응답을 요약에만 적어 두고 그대로
		//   frozen 으로 끝내면, 읽는 쪽은 "그 스레드가 조용했다" 와 "못 받았다"
		//   를 구분할 수 없다. 그래서 캡처 자신이 들고 다닌다.
		bool          complete() const { return m_complete; }
		std::uint32_t unacked_streams() const { return m_unackedStreams; }

		const frame_record* find_frame(std::uint32_t engine_frame) const;

	private:
		std::vector<frame_record>   m_frames;
		std::vector<thread_info>    m_threads;
		std::vector<capture_marker> m_markers;
		std::vector<capture_counter> m_counters;
		capture_environment         m_environment{};
		std::size_t               m_memoryBytes = 0;
		std::uint64_t             m_totalEvents = 0;
		std::uint64_t             m_droppedCounters = 0;
		bool                      m_complete = true;
		std::uint32_t             m_unackedStreams = 0;
	};

	using capture_session_ptr = std::shared_ptr<const capture_session>;

	// 녹화 중 계속 쓰는 쪽. 수집 스레드(프레임 경계를 도는 쪽) 하나만 만진다.
	class capture_ring
	{
	public:
		void configure(std::uint32_t retained_frames, std::size_t memory_budget);
		void clear();

		// 봉인된 청크 목록의 소유권을 프레임 페이지로 옮긴다. 마지막 캡처가
		// 페이지를 놓을 때 풀에 되돌아가므로, 읽는 쪽은 이벤트를 복사하지 않는다.
		// generation 과 다른 세대의 청크는 버리고 센다(Clear 이전의 것).
		// frame_begin_tick 은 지금 열려 있는 프레임의 시작 시각이고, 그보다
		// 앞서 끝난 이벤트는 **자기 시각이 속한 프레임 칸**으로 돌려보낸다.
		void ingest(event_chunk* sealed_list, std::shared_ptr<chunk_pool> pool,
		            std::uint64_t generation, profile_tick frame_begin_tick);

		// 이 프레임을 닫고 다음 프레임을 연다.
		void close_frame(std::uint32_t engine_frame, profile_tick tick_begin, profile_tick tick_end);
		// Collector-only. Late owner samples keep their original engine frame.
		void record_counter(std::uint32_t engine_frame, profile_counter_sample sample);
		void discard_deferred_counters_before(std::uint32_t engine_frame);
		void note_dropped_counter() { ++m_droppedCounters; }
		std::uint64_t dropped_counters() const { return m_droppedCounters; }

		// 이번 프레임에서 잃은 수와 누적을 함께 센다. 프레임 기록에는 이번
		// 프레임 몫만 들어가고, 누적은 링이 들고 있다.
		void note_dropped(std::uint64_t count)
		{
			m_pendingDropped += count;
			m_droppedEvents += count;
		}

		// 지금까지 모인 것을 얼려 공개한다. 링은 비우지 않는다 — 다시
		// 녹화를 눌러도 앞이 남아 있어야 하기 때문이다.
		capture_session_ptr freeze(std::span<const thread_info> threads,
		                           capture_environment environment,
		                           bool complete, std::uint32_t unacked_streams) const;

		std::uint32_t retained_frames() const { return static_cast<std::uint32_t>(m_frames.size()); }
		std::size_t   memory_bytes() const { return m_memoryBytes; }
		std::uint64_t dropped_events() const { return m_droppedEvents; }

		// 가장 최근에 닫힌 프레임의 이벤트 수. 녹화 중에도 값싸게 읽히는
		// 요약이라 게이트와 HUD 가 이것을 본다(§6.4 의 live summary).
		std::uint32_t last_frame_events() const { return m_lastFrameEvents; }

		// 아직 닫히지 않은 프레임에 모인 것이 있는가. pause 가 본다 — 마지막
		// 프레임을 닫지 않고 얼리면 그 사이의 것이 통째로 사라진다.
		bool has_pending_events() const { return !m_pending.events.empty(); }
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
		std::uint64_t stale_chunks_dropped() const { return m_staleChunksDropped; }
		std::uint64_t malformed_pages() const { return m_malformedPages; }
		std::uint64_t ingested_pages() const { return m_ingestedPages; }
		std::uint64_t late_events_placed() const { return m_lateEventsPlaced; }
		std::uint64_t late_events_dropped() const { return m_lateEventsDropped; }
		std::size_t   late_spans_waiting() const { return m_deferredSpans.size(); }

	private:
		void trim();

		// 이 구간을 제 프레임 칸에 넣는다. 그 프레임이 아직 안 닫혔으면
		// 기다리게 두고, 이미 밀려났으면 버리고 센다.
		void place_late_span(const profile_event& value,
		                     const std::shared_ptr<const event_chunk>& page = {},
		                     std::uint32_t offset = 0);
		void drain_deferred_spans();

		// 늦게 온 CPU 구간. GPU 와 규칙이 다르다 — GPU 는 **제출한 프레임**에
		// 귀속하지만(§5.4), CPU 구간은 **끝난 시각이 속한 프레임**이 제 자리다.
		// 프레임을 넘는 구간이 닫히는 프레임에 기록되던 것과 같은 답이 나온다.
		bool place_by_tick(const profile_event& value,
		                   const std::shared_ptr<const event_chunk>& page,
		                   std::uint32_t offset);

		std::vector<frame_record> m_frames;
		frame_record              m_pending;
		struct deferred_counter { std::uint32_t frame; profile_counter_sample sample; };
		std::vector<deferred_counter> m_deferredCounters;
		std::uint64_t m_droppedCounters = 0;

		// 아직 제 프레임이 닫히지 않아 기다리는 구간. 상한을 두는 이유는
		// 프레임이 영영 안 닫히는 경우(녹화를 멈춘 채 GPU 만 도는 경우)에
		// 이 목록이 무한히 자라지 않게 하기 위해서다.
		static constexpr std::size_t kMaxDeferredSpans = 4096;
		std::vector<profile_event> m_deferredSpans;
		std::uint64_t m_lateSpansPlaced = 0;
		std::uint64_t m_lateSpansDropped = 0;
		std::uint64_t m_staleChunksDropped = 0;
		std::uint64_t m_malformedPages = 0;
		std::uint64_t m_ingestedPages = 0;
		std::uint64_t m_lateEventsPlaced = 0;
		std::uint64_t m_lateEventsDropped = 0;

		std::uint32_t m_retainedFrames = kDefaultRetainedFrames;
		std::size_t   m_memoryBudget = kDefaultMemoryBudget;
		std::size_t   m_memoryBytes = 0;
		std::uint64_t m_droppedEvents = 0;
		std::uint64_t m_pendingDropped = 0;   // 아직 닫히지 않은 프레임의 몫
		std::uint32_t m_lastFrameEvents = 0;
		std::uint32_t m_peakFrameEvents = 0;
	};
}
