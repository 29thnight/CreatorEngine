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

		// --- recorder ----------------------------------------------------
		// 시작 프레임 번호를 받는다. 이것이 없으면 첫 스코프들이 "아직 모르는"
		// 프레임에 기록되고, 그 프레임을 닫을 때 붙는 라벨과 어긋난다 —
		// 프레임을 넘는 구간의 시작 프레임이 틀어지는 것이 그 증상이다.
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

	private:
		thread_stream* current_stream();
		void           collect_sealed();
		void           destroy_stream_locked(std::size_t index);

		struct stream_entry
		{
			std::unique_ptr<thread_stream> stream;
			std::uint32_t                  os_thread_id = 0;
			bool                           live = false;
		};

		std::uint32_t m_serviceSlot = 0;

		std::atomic<bool>           m_initialized{ false };
		std::atomic<recorder_state> m_state{ recorder_state::stopped };
		std::atomic<std::uint32_t>  m_engineFrame{ 0 };

		chunk_pool m_pool;

		mutable std::mutex        m_streamLock;
		std::vector<stream_entry> m_streams;
		std::vector<thread_info>  m_threadInfo;

		// 수집기(프레임 경계를 도는 스레드)만 만진다.
		capture_ring m_ring;
		profile_tick m_frameBeginTick = 0;

		mutable std::mutex  m_captureLock;
		capture_session_ptr m_capture;

		// 스트림이 사라질 때 그 스레드의 계수를 서비스로 옮긴다 — 스레드가
		// 죽었다고 해서 잃은 수가 없던 일이 되면 안 된다.
		std::atomic<std::uint64_t> m_retiredDropped{ 0 };
		std::atomic<std::uint64_t> m_retiredUnbalanced{ 0 };
	};
}
