#pragma once
// PHASE 14 P1+P2 — 프로파일러 서비스.
//
// ★ 스트림의 소유자는 서비스다. 옛 코어는 `GetTLSUnsafe()` 가 함수 지역
//   `static thread_local` 이라 **모든 인스턴스가 스레드당 TLS 하나를 공유**했고,
//   그래서 검사 전용 인스턴스를 세울 수 없어 selftest 가 라이브 캡처의 프레임
//   경계를 직접 넘겨야 했다(그 교란 때문에 stats 를 selftest 직후에 재면
//   포화로 보인다). 여기서는 thread_local 이 **슬롯 배열**이고 서비스마다
//   자기 자리를 가지므로, 검사용 서비스와 라이브 서비스가 같은 스레드에서
//   동시에 살아 서로를 건드리지 않는다.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#include "ProfileCapture.h"
#include "ProfileMarker.h"
#include "ProfileThreadStream.h"

namespace ce
{
	enum class recorder_state : std::uint8_t
	{
		stopped = 0,     // marker 는 즉시 return
		recording = 1,   // rolling ring 에 계속 기록
		frozen = 2,      // producer 를 멈추고 immutable capture 를 공개

		// 멈추라고는 했는데 아직 모두의 꼬리를 받지 못했다. 수집기가 다음
		// 프레임 경계에서 마무리한다.
		//
		// ★ 이 상태가 없으면 Pause 를 부른 쪽이 완료까지 **기다려야** 한다.
		//   UI 는 씬 잠금을 쥔 채 부르고 게임 스레드는 같은 잠금을 통과해야
		//   요청을 처리하므로, 기다리는 순간 서로를 기다린다(실측 2,000 ms
		//   상한까지 멈췄고 돌아왔을 때도 여전히 recording 이었다).
		pausing = 3,
	};

	struct profiler_config
	{
		std::uint32_t chunk_count = 256;                       // 청크 풀 크기
		std::uint32_t retained_frames = kDefaultRetainedFrames;
		std::size_t   memory_budget = kDefaultMemoryBudget;
	};

	// 녹화 중에도 값싸게 읽히는 요약(§6.4). 전체 캡처를 복사하지 않는다.
	struct live_summary
	{
		recorder_state state = recorder_state::stopped;
		std::uint32_t  engine_frame = 0;
		std::uint32_t  retained_frames = 0;
		std::uint32_t  last_frame_events = 0;
		std::uint32_t  peak_frame_events = 0;
		std::uint64_t  total_events = 0;
		std::uint64_t  dropped_events = 0;
		std::uint64_t  unbalanced_scopes = 0;
		std::uint32_t  thread_count = 0;
		std::uint32_t  registered_markers = 0;
		std::size_t    memory_bytes = 0;
		std::size_t    memory_budget = 0;
		std::uint32_t  free_chunks = 0;
		std::uint32_t  chunk_count = 0;

		// GPU 레인(§7.3). 늦게 온 구간이 제 프레임 칸을 찾았는가.
		std::uint64_t  late_spans_placed = 0;
		std::uint64_t  late_spans_dropped = 0;
		std::size_t    late_spans_waiting = 0;

		// pause 에서 봉인 요청에 **응답하지 않은** 스트림 수. 0 이 아니면 그
		// 스트림의 꼬리가 이 캡처에 없다 — 잠든 워커가 대표적이다.
		//
		// ★ 이 값이 0 이 아니면 얼린 캡처는 **온전하지 않다.** 창과 게이트가
		//   그것을 알아야 "비었다" 와 "못 받았다" 를 가릴 수 있다.
		std::uint32_t  pause_unacked_streams = 0;
		bool           capture_complete = true;

		// 주인이 아닌 스레드가 남의 스트림을 만지려 한 횟수. **0 이어야 한다.**
		// 0 이 아니면 그 호출은 아무 일도 하지 않았고, 그만큼의 기록이 없다.
		std::uint64_t  foreign_stream_touches = 0;

		// 주인이 살아 있어 닫지 못한 채 회수한 스트림 수. 그만큼의 꼬리가
		// 어느 캡처에도 없다 — 종료 전에 unregister_thread 를 부르지 않은 것이다.
		std::uint64_t  abandoned_streams = 0;

		// 다른 스레드에서 들어와 수집기로 넘긴 제어 요청 수.
		std::uint64_t  control_requests_deferred = 0;

		// 지운 세대의 것이라 버린 이벤트, 시각이 링 밖이라 버린 이벤트.
		std::uint64_t  stale_chunks_dropped = 0;
		std::uint64_t  late_events_placed = 0;
		std::uint64_t  late_events_dropped = 0;
	};

	// 한 프로세스에 동시에 살 수 있는 서비스 수. 라이브 하나 + 검사용 하나면
	// 충분하지만, 상수를 빠듯하게 잡아 두면 나중에 조용히 덮어쓴다.
	inline constexpr std::uint32_t kMaxLiveServices = 4;

	class profiler_service
	{
	public:
		profiler_service();
		~profiler_service();

		profiler_service(const profiler_service&) = delete;
		profiler_service& operator=(const profiler_service&) = delete;

		void initialize(const profiler_config& config = {});
		void shutdown();
		bool is_initialized() const { return m_initialized.load(std::memory_order_acquire); }

		// --- 스레드 ------------------------------------------------------
		// 스트림을 서비스가 만들어 소유한다. 등록한 스레드가 끝나면
		// unregister_thread 를 부르고, 안 불러도 서비스 종료 시 정리된다 —
		// 어느 쪽이든 수집기가 죽은 저장소를 읽는 경로가 없다.
		void register_thread(const char* name = nullptr);
		void unregister_thread();
		std::uint32_t thread_count() const;

		// 지금 등록되어 있는 스레드. 얼린 캡처가 없어도 읽힌다 — 녹화 중에
		// 스레드가 잡히고 있는지 보는 것이 라이브 기준선의 축 하나다.
		std::vector<thread_info> threads() const;

		// --- 기록 --------------------------------------------------------
		// hot path. 상태가 recording 이 아니면 곧바로 돌아온다.
		void begin_scope(marker_id id);
		void end_scope();

		// 프레임 경계. 엔진 프레임 번호는 밖에서 받는다 — 프로파일러가
		// 자기 카운터를 따로 세면 그 수가 엔진의 어느 프레임인지 아무도
		// 모르게 된다(§2.1 engine_frame_id 정본 통합).
		void publish_frame(std::uint32_t engine_frame);

		// --- GPU 레인 ----------------------------------------------------
		// 이미 끝난 GPU 구간을 전용 레인에 적는다(§7.3 의 GPU Graphics queue).
		//
		// ★ 틱은 **CPU(QPC) 축으로 옮긴 뒤**의 값이어야 한다. 이 층은 GPU 틱도
		//   두 시계의 관계도 모른다 — 옮기는 일은 백엔드의 몫이고, 여기는
		//   "이미 CPU 축에 있는 구간" 만 받는다.
		//
		// ★ 레인의 스트림은 **적는 쪽만** 봉인한다. publish_frame 은 이 레인을
		//   건너뛴다 — 적는 스레드가 렌더 스레드이고 프레임 경계를 도는 쪽은
		//   게임 스레드라, 남이 봉인하면 두 스레드가 같은 청크 포인터를 만진다.
		void submit_gpu_span(marker_id id, profile_tick begin, profile_tick end,
		                     std::uint32_t frame);

		// 지금까지 적은 GPU 구간을 수집기에 넘긴다. 적는 스레드가 부른다.
		void publish_gpu_spans();

		// 레인을 은퇴시킨다. **적던 스레드가** 멎기 전에 부른다.
		//
		// ★ 이 레인은 OS 스레드가 아니라 큐지만, 저장소의 주인은 여전히
		//   **처음 적은 스레드**다. 그 스레드가 unregister_thread 를 불러도
		//   자기 TLS 스트림만 끊을 뿐 이 레인은 남는다 — 그래서 종료 때
		//   게임 스레드가 남의 저장소를 마주하고 버려진 것으로 센다.
		//   부른 쪽이 주인이 아니면 아무것도 하지 않는다.
		void retire_gpu_lane();

		// GPU 레인의 이름. 창과 게이트가 이 이름으로 레인을 찾는다.
		static constexpr const char* kGpuLaneName = "[GPU Graphics]";

		// pause 가 봉인 응답을 기다리는 횟수. 상한을 두는 이유는 잠든
		// producer 가 영영 안 깨어날 수 있기 때문이다.
		static constexpr int kPauseAckAttempts = 256;

		// --- recorder ----------------------------------------------------
		// 시작 프레임 번호를 받는다. 이것이 없으면 첫 스코프들이 "아직 모르는"
		// 프레임에 기록되고, 그 프레임을 닫을 때 붙는 라벨과 어긋난다 —
		// 프레임을 넘는 구간의 시작 프레임이 틀어지는 것이 그 증상이다.
		// ★ 이 셋은 **수집기의 일**이다. 링은 프레임 경계를 도는 스레드의
		//   것이고, 창이나 콘솔 스레드가 여기서 직접 링을 만지면 수집기와
		//   겹친다 — 스트림에서 그랬던 것과 같은 경계다.
		//
		//   그래서 수집기 스레드에서 불리면 그 자리에서 하고, 다른 스레드에서
		//   불리면 **요청으로 줄을 세운 뒤 다음 프레임 경계에서** 적용된다.
		//   **부른 쪽은 기다리지 않는다.** 수집기가 한 번도 돈 적이 없으면
		//   (프레임이 안 도는 검사용 서비스) 그 자리에서 한다.
		//
		//   그래서 pause() 뒤의 상태는 frozen 이 아니라 pausing 일 수 있다.
		//   캡처가 필요하면 state 가 frozen 이 될 때까지 기다려야 한다.
		void record(std::uint32_t first_frame = 0);
		void pause();
		void clear();
		recorder_state state() const { return m_state.load(std::memory_order_acquire); }

		// 얼린 캡처. pause() 뒤에 유효하다.
		capture_session_ptr capture() const;

		live_summary summary() const;

		// QPC 주파수(틱/초). 틱을 시간으로 바꾸는 유일한 기준.
		static profile_tick ticks_per_second();
		static profile_tick now();

		// 종료에서 해제하지 못하고 **놓아 둔** 스트림의 수(프로세스 전체 누적).
		//
		// ★ 주인이 아직 돌고 있는 스트림은 파괴할 수 없다. 남의 finish() 를
		//   피해도 곧바로 저장소를 해제하면 같은 일이고, 소멸자 자신이 소유
		//   검사 없이 seal_current() 를 부른다. 확인되지 않은 것은 놓아 두고
		//   **센다** — 종료 전에 unregister_thread 를 부르지 않았다는 뜻이다.
		static std::size_t retained_stream_count();

	private:
		thread_stream* current_stream();

		// 이 스레드의 자리. 세대가 어긋나면 지난 서비스의 것이므로 없는 것으로
		// 읽는다 — 남의 스레드의 자리는 shutdown 이 끊을 수 없기 때문이다.
		thread_stream* tls_stream() const;

		// --- 제어 요청 줄 -------------------------------------------------
		enum class control_op : std::uint8_t { record, pause, clear, finish_pause };

		struct control_request
		{
			control_op    op = control_op::pause;
			std::uint32_t frame = 0;
		};

		// 지금 이 스레드가 수집기인가. 수집기가 아직 없으면 true 를 낸다 —
		// 기다릴 대상이 없으므로 그 자리에서 하는 것이 맞다.
		bool on_collector() const;

		// 요청을 세우고 적용될 때까지 짧게 기다린다.
		void dispatch_control(control_op op, std::uint32_t frame);

		// 줄에 선 것을 전부 적용한다. 수집기만 부른다.
		void apply_control_requests();

		void record_now(std::uint32_t first_frame);
		void pause_now();
		void clear_now();

		// 얼림을 마무리한다. 봉인 응답을 확인하고 링을 얼려 공개한다.
		// **수집기만** 부른다 — 링을 만지기 때문이다.
		void finish_pause();

		// 얼림을 청한 시각. pause 를 부른 순간 한 번 정해지고, 모든 스트림이
		// 같은 시각에서 잘린다.
		std::atomic<profile_tick> m_freezeTick{ 0 };
		void           collect_sealed();
		void           destroy_stream_locked(std::size_t index);

		// 링에서 뽑은 수치. **수집기만** 만드는 값이고, 읽는 쪽은 여기 찍힌
		// 사본만 본다.
		//
		// ★ 예전에는 summary() 가 m_ring 을 직접 읽었다. 링은 수집기가
		//   고치는 중인 자료라, 창이나 콘솔 스레드가 그것을 읽으면 고쳐지는
		//   도중의 vector 를 보는 것이다 — 값이 흔들리는 정도가 아니라
		//   size 와 저장소가 어긋난 순간을 밟을 수 있다.
		struct ring_stats
		{
			std::uint32_t retained_frames = 0;
			std::uint32_t last_frame_events = 0;
			std::uint32_t peak_frame_events = 0;
			std::uint64_t dropped_events = 0;
			std::size_t   memory_bytes = 0;
			std::uint64_t late_spans_placed = 0;
			std::uint64_t late_spans_dropped = 0;
			std::size_t   late_spans_waiting = 0;
			std::uint64_t stale_chunks_dropped = 0;
			std::uint64_t late_events_placed = 0;
			std::uint64_t late_events_dropped = 0;
		};

		// 지금 링의 상태를 찍어 공개한다. 수집기가 부른다.
		void publish_ring_stats();

		struct stream_entry
		{
			std::unique_ptr<thread_stream> stream;
			std::uint32_t                  os_thread_id = 0;
			bool                           live = false;

		};

		// GPU 레인의 스트림 자리. 아직 만들지 않았으면 비어 있다.
		thread_stream* gpu_stream();

		// 원자로 둔다. 만드는 것은 한 번이지만 읽는 쪽이 lock 없이 들어오므로,
		// 평범한 포인터면 초기화가 보이지 않는 창이 생긴다.
		std::atomic<thread_stream*> m_gpuStream{ nullptr };

		// 마지막 pause 에서 응답하지 않은 스트림 수.
		std::atomic<std::uint32_t> m_pauseUnacked{ 0 };

		// 녹화 세대. clear() 가 올린다. 그 전에 열린 청크가 나중에 도착하면
		// 세대가 어긋나고 수집기가 버린다 — 지운 것이 돌아오지 않게.
		std::atomic<std::uint64_t> m_generation{ 1 };

		std::uint32_t m_serviceSlot = 0;

		// 이 서비스가 thread_local 자리에 찍는 번호. shutdown 에서 올라간다.
		std::uint64_t m_slotEpoch = 0;

		// 프레임 경계를 도는 스레드. 첫 publish_frame 이 정한다.
		std::atomic<std::thread::id> m_collectorThread{ std::thread::id{} };

		std::mutex                   m_controlLock;
		std::vector<control_request> m_controlQueue;
		std::atomic<std::uint64_t>   m_controlEnqueued{ 0 };
		std::atomic<std::uint64_t>   m_controlApplied{ 0 };
		std::atomic<std::uint64_t>   m_controlDeferred{ 0 };

		std::atomic<bool>           m_initialized{ false };
		std::atomic<recorder_state> m_state{ recorder_state::stopped };
		std::atomic<std::uint32_t>  m_engineFrame{ 0 };

		// ★ shared_ptr 인 이유는 **놓아 둔 스트림이 풀보다 오래 살기** 때문이다.
		//   주인이 아직 도는 스트림을 종료에서 파괴할 수 없으므로 놓아 두는데,
		//   그 스트림은 계속 이 풀에 청크를 청한다. 풀을 먼저 접으면 놓아 둔
		//   의미가 없다.
		std::shared_ptr<chunk_pool> m_pool = std::make_shared<chunk_pool>();

		mutable std::mutex        m_streamLock;
		std::vector<stream_entry> m_streams;
		std::vector<thread_info>  m_threadInfo;

		// 수집기(프레임 경계를 도는 스레드)만 만진다.
		capture_ring m_ring;

		// 그 링에서 찍어 낸 사본. 읽는 쪽은 이것만 본다.
		mutable std::mutex m_ringStatsLock;
		ring_stats         m_ringStats;
		profile_tick m_frameBeginTick = 0;

		// 마지막으로 닫은 엔진 프레임 번호. pause 가 남은 프레임을 닫을 때
		// 그다음 번호를 쓴다 — 얼린 캡처의 꼬리도 어느 프레임인지 말해야 한다.
		std::uint32_t m_lastEngineFrame = 0;

		mutable std::mutex  m_captureLock;
		capture_session_ptr m_capture;

		// 스트림이 사라질 때 그 스레드의 계수를 서비스로 옮긴다 — 스레드가
		// 죽었다고 해서 잃은 수가 없던 일이 되면 안 된다.
		std::atomic<std::uint64_t> m_retiredDropped{ 0 };
		std::atomic<std::uint64_t> m_retiredUnbalanced{ 0 };
		std::atomic<std::uint64_t> m_retiredForeign{ 0 };
		std::atomic<std::uint64_t> m_abandonedStreams{ 0 };
	};
}
