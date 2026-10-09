#pragma once
// PHASE 14 P2 — 스레드 스트림과 청크 풀.
//
// 소유권이 이 파일의 전부다:
//
//   chunk_pool   free 청크를 나눠 주고 되받는다. 봉인된 청크의 유일한 주인.
//   thread_stream  writer 하나가 쓰는 현재 청크와 열린 스코프 스택.
//
// writer 는 자기 thread_stream 만 만진다. 수집기는 pool 의 sealed 목록을
// 소비하고 스트림의 원자적 응답 상태만 읽는다. 남의 TLS 나 현재 쓰는 청크를
// 직접 읽던 옛 경로는 없다.
#include <atomic>
#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "ProfileEvent.h"

namespace ce
{
	// §7.3 이 정한 트랙 순서. 타임라인의 레인은 이 값으로 선다.
	//
	// ★ 이름을 뜯어 짐작하지 않는다. 코어가 `"[Worker "` 같은 접두사를 알면
	//   이름을 바꾸는 순간 순서가 **조용히** 무너지고, 그때 무너진 것을
	//   가리키는 것이 아무것도 없다. 등록하는 쪽이 자기가 어느 트랙인지
	//   말한다 — worker_hooks 와 같은 뒤집기다.
	//
	// ★ 슬롯 오름차순으로는 안 된다. 슬롯은 **등록 순서**이고 워커의 등록
	//   순서는 회차마다 갈린다(실측: `[Worker 1], [Worker 5], [Worker 7] …`
	//   다음 회차 `[Worker 2], [Worker 5], [Worker 1] …`). 그러면 같은 코드로
	//   레인 순서가 번갈아 나와 무엇도 고정할 수 없다.
	enum class track_kind : std::uint8_t
	{
		frame_boundary = 0,   // 프레임 경계·instant
		game_thread    = 1,   // CPU main/game
		command_thread = 2,   // command-build/command-execute/worker
		script_thread  = 3,   // managed/script
		gpu_graphics   = 4,   // GPU Graphics queue
		gpu_compute    = 5,   // GPU Compute/Copy queue
		physics_worker = 7,   // PhysX SDK worker; independent from render command threads
		other          = 6,   // 말하지 않은 것은 맨 아래
	};

	// 한 스레드가 프로파일러에 보이는 이름과 자리.
	struct thread_info
	{
		std::string   name;
		std::uint32_t os_thread_id = 0;
		std::uint32_t slot = 0;

		// 어느 트랙에 속하는가, 그리고 그 트랙 **안에서** 몇 번째인가.
		//
		// ★ track_order 가 따로 있는 이유는 슬롯이 못 쓰는 자이기 때문이다.
		//   enkiTS 의 `threadnum_` 은 0..N-1 로 안정 보장되므로 워커는 그것을
		//   넘긴다 — 등록이 어떤 순서로 겹치든 레인은 1,2,3… 으로 선다.
		track_kind    kind = track_kind::other;
		std::uint32_t track_order = 0;
	};

	// 레인 순서(§7.3). 트랙 → 트랙 안의 순서 → 슬롯.
	//
	// ★ 이 비교가 **한 벌이어야 한다.** 타임라인이 보는 순서와 CLI 가 내는
	//   순서를 따로 적으면, 한쪽만 고쳐지는 날 화면과 게이트가 서로 다른
	//   순서를 말하면서 둘 다 자기가 옳다고 한다.
	//
	// ★ inline 이 아니라 .cpp 에 둔다. 변이 하네스가 바꿔 치는 단위가 .cpp 라,
	//   헤더에 두면 이 비교에 이빨이 있는지 물을 수단이 없다.
	bool track_precedes(const thread_info& a, const thread_info& b);

	// 청크를 나눠 주고 봉인된 것을 모은다. free 가 없으면 필요할 때
	// 페이지를 확장한다. 명시적 상한 또는 할당 실패만 누락으로 남긴다.
	class chunk_pool
	{
	public:
		chunk_pool() = default;
		~chunk_pool();

		chunk_pool(const chunk_pool&) = delete;
		chunk_pool& operator=(const chunk_pool&) = delete;

		void initialize(std::uint32_t chunk_count, std::uint32_t maximum_chunks);
		void shutdown();
		// 봉인 목록이 빈 상태에서 첫 페이지가 들어왔을 때만 깨운다.
		// 종료 시 nullptr 로 끊어, 서비스보다 오래 사는 풀도 안전하게 남긴다.
		void set_signal(std::counting_semaphore<INT_MAX>* signal);

		// free 목록에서 하나 꺼낸다. 비어 있으면 기록자가 페이지를 할당한다.
		// 명시적 상한 또는 할당 실패 시에만 nullptr 를 반환한다.
		event_chunk* acquire();

		// writer 가 다 쓴 청크를 넘긴다. 이 호출 뒤 writer 는 그 포인터를
		// 다시 쓰지 않는다.
		void seal(event_chunk* chunk);

		// 봉인된 목록을 통째로 떼어 온다. 수집기만 부른다.
		event_chunk* take_sealed();

		// 수집이 끝난 청크를 free 로 되돌린다.
		void release(event_chunk* chunk_list);

		// 보존 캡처가 페이지를 붙잡아도 writer 는 기다리지 않는다. 부족분은
		// 수집 스레드가 미리 할당해 free 목록에 보충한다.
		void replenish(std::uint32_t target_free, std::uint32_t maximum_chunks);

		std::uint32_t chunk_count() const;
		std::uint32_t free_count() const;

	private:
		std::vector<std::unique_ptr<event_chunk>> m_storage;

		mutable std::mutex m_lock;
		// Sealed FIFO publication must not wait behind allocation or free-list maintenance.
		mutable std::mutex m_sealedLock;
		event_chunk*  m_free = nullptr;
		event_chunk*  m_sealed = nullptr;
		event_chunk*  m_sealedTail = nullptr;
		std::uint32_t m_chunkCount = 0;
		std::uint32_t m_freeCount = 0;
		std::uint32_t m_maximumChunks = 0;
		std::counting_semaphore<INT_MAX>* m_signal = nullptr;
	};

    // 논리적인 begin 하나의 자리. 기록하지 않은 begin도 token == 0인 자리를
    // 차지해야, 녹화 도중 들어온 안쪽 scope의 end가 바깥쪽 skip을 소비하지 않는다.
    struct open_scope
    {
        cpu_span_context cpu;
        profile_tick tick_begin = 0;
        marker_id marker = invalid_marker;
        std::uint32_t frame = 0;
        std::uint16_t depth = 0;
        event_flags flags = event_flags::none;
        std::uint64_t generation = 0;
        std::uint64_t token = 0;
    };

    inline constexpr std::uint32_t kMaxScopeDepth = 64;

    // 스택과 현재 청크는 주인만 만진다. 수집기는 원자적으로 공개한 시작점과
    // 봉인된 청크만 읽고, 실제 종료와 Stop의 잘린 종료는 같은 token을 선점한다.
    class thread_stream
    {
    public:
        thread_stream(chunk_pool& pool, thread_info info);
        ~thread_stream();

        thread_stream(const thread_stream&) = delete;
        thread_stream& operator=(const thread_stream&) = delete;

        void begin_scope(marker_id id, profile_tick now, std::uint32_t frame,
                         const cpu_span_context& cpu = {});
        void begin_scope(marker_id id, profile_tick now, std::uint32_t frame,
                         const cpu_span_context& cpu, std::uint64_t generation);
        void end_scope(profile_tick now);
        void skip_scope();

        void write_span(marker_id id, profile_tick begin, profile_tick end,
                        std::uint32_t frame, std::uint16_t depth, const gpu_span_context& gpu);
        void write_instant(marker_id id, profile_tick tick, std::uint32_t frame,
                           const cpu_span_context& cpu = {});
        void write_instant(marker_id id, profile_tick tick, std::uint32_t frame,
                           const cpu_span_context& cpu, std::uint64_t generation);

        void publish_frame();
        void truncate_open_scopes(profile_tick freeze_tick);
        void freeze_self(profile_tick freeze_tick);
        void finish(profile_tick now);

        void request_seal()
        {
            m_sealRequest.fetch_add(1, std::memory_order_release);
        }

        // 제어 호출끼리는 서비스의 stream lock으로 직렬화한다. 기록자는
        // 잠금 없이 요청 한 벌의 버전을 확인하고, 처리한 그 요청만 응답한다.
        void request_freeze(profile_tick freeze_tick);

        std::uint64_t freeze_request() const
        {
            return m_freezeRequest.load(std::memory_order_acquire);
        }
        std::uint64_t freeze_ack() const
        {
            return m_freezeAck.load(std::memory_order_acquire);
        }
        std::uint64_t seal_request() const
        {
            return m_sealRequest.load(std::memory_order_acquire);
        }
        std::uint64_t seal_ack() const
        {
            return m_sealAck.load(std::memory_order_acquire);
        }
        bool pending_work() const
        {
            return m_pendingState.load(std::memory_order_seq_cst) != 0;
        }
        void set_generation(std::uint64_t value)
        {
            m_generation.store(value, std::memory_order_release);
        }

        // 호출자는 스트림의 수명을 보장한다. 두 함수 모두 TLS/현재 청크를
        // 읽지 않으며, end_tick 뒤에 시작한 구간과 다른 세대는 제외한다.
        // snapshot은 일시적인 표시용이다. 완성 이벤트와 함께 저장하지 않는다.
        std::uint32_t snapshot_open_scopes(std::uint64_t generation, profile_tick end_tick,
                                          profile_event* output, std::uint32_t capacity) const;

        // Stop의 미응답 스트림에만 쓴다. 반환된 구간은 호출자가 캡처에 넣어야
        // 한다. 선점 후 주인이 깨어나도 같은 구간을 다시 적을 수 없다.
        std::uint32_t claim_open_scopes(std::uint64_t generation, profile_tick end_tick,
                                       profile_event* output, std::uint32_t capacity);

        const thread_info& info() const { return m_info; }
        std::uint32_t slot() const { return m_info.slot; }
        std::uint32_t open_depth() const { return m_publishedDepth.load(std::memory_order_acquire); }
        std::thread::id owner_thread() const { return m_ownerThread; }

        std::uint64_t dropped_events() const
        {
            return m_droppedEvents.load(std::memory_order_relaxed);
        }
        std::uint64_t dropped_scopes() const
        {
            return m_droppedScopes.load(std::memory_order_relaxed);
        }
        std::uint64_t unbalanced_scopes() const
        {
            return m_unbalancedScopes.load(std::memory_order_relaxed);
        }
        std::uint64_t stale_scopes() const
        {
            return m_staleScopes.load(std::memory_order_relaxed);
        }
        std::uint64_t stale_events() const
        {
            return m_staleEvents.load(std::memory_order_relaxed);
        }
        std::uint64_t foreign_touches() const
        {
            return m_foreignTouches.load(std::memory_order_relaxed);
        }

    private:
        // token의 최하위 비트가 1이면 열린 구간이다. 같은 자리를 재사용할
        // 때마다 다른 token을 쓰므로, 복사 도중 재사용된 metadata는 선점하지 못한다.
        // 모든 필드가 원자적이어야 실패한 복사도 C++ data race가 되지 않는다.
        struct published_scope
        {
            std::atomic<std::uint64_t> token{ 0 };
            std::atomic<std::uint64_t> generation{ 0 };
            std::atomic<profile_tick> tick_begin{ 0 };
            std::atomic<marker_id> marker{ invalid_marker };
            std::atomic<std::uint32_t> frame{ 0 };
            std::atomic<std::uint16_t> depth{ 0 };
            std::atomic<event_flags> flags{ event_flags::none };
            std::atomic<std::uint64_t> session{ 0 };
            std::atomic<std::uint64_t> tick{ 0 };
            std::atomic<std::uint64_t> task{ 0 };
        };

        bool ensure_chunk(std::uint64_t generation);
        void write(const profile_event& value, std::uint64_t generation, bool late_ingest = false);
        void seal_current(bool writing = false);
        void honor_seal_request();
        bool owned_by_caller() const;
        void push_skipped_scope();
        std::uint16_t recording_depth(std::uint64_t generation) const;
        void publish_open_scope(std::uint32_t index);
        bool claim_scope(std::uint32_t index);
        bool read_open_scope(std::uint32_t index, std::uint64_t generation,
                             profile_tick end_tick, profile_event& value, std::uint64_t& token) const;
        void truncate_open_scopes(profile_tick freeze_tick, std::uint64_t generation);

        chunk_pool& m_pool;
        thread_info m_info;
        event_chunk* m_writer = nullptr;
        std::uint64_t m_sequence = 0;

        open_scope m_stack[kMaxScopeDepth]{};
        published_scope m_publishedScopes[kMaxScopeDepth]{};
        std::uint32_t m_depth = 0;
        std::atomic<std::uint32_t> m_publishedDepth{ 0 };
        // 상한 밖의 자리만 개수로 센다. 이 구간에는 어떤 begin도 기록하지
        // 않으므로 언제나 스택보다 안쪽이며, 세대가 바뀌어도 짝이 유지된다.
        std::uint64_t m_overflowDepth = 0;
        std::uint64_t m_nextScopeToken = 1;

        std::atomic<std::uint64_t> m_sealRequest{ 0 };
        std::atomic<std::uint64_t> m_sealAck{ 0 };
        std::atomic<std::uint64_t> m_freezeVersion{ 0 };
        std::atomic<profile_tick> m_freezeTick{ 0 };
        std::atomic<std::uint64_t> m_freezeGeneration{ 0 };
        std::atomic<std::uint64_t> m_freezeRequest{ 0 };
        std::atomic<std::uint64_t> m_freezeAck{ 0 };
        std::atomic<profile_tick> m_frozenTick{ 0 };
        std::atomic<std::uint64_t> m_frozenGeneration{ UINT64_MAX };
        // 두 상태를 한 원자에 두어 seal과 begin의 중간을 idle로 읽지 않는다.
        // admission/cutoff/관측의 seq_cst 순서가, 서로 다른 두 원자의 옛 값을
        // 양쪽에서 함께 읽어 Stop이 새 begin을 놓치는 store-buffering을 막는다.
        static constexpr std::uint8_t kPendingWork = 1;
        static constexpr std::uint8_t kPendingAdmission = 2;
        std::atomic<std::uint8_t> m_pendingState{ 0 };
        bool m_inHonor = false;
        std::atomic<std::uint64_t> m_generation{ 0 };

        std::atomic<std::uint64_t> m_droppedEvents{ 0 };
        std::atomic<std::uint64_t> m_droppedScopes{ 0 };
        std::atomic<std::uint64_t> m_unbalancedScopes{ 0 };
        std::atomic<std::uint64_t> m_staleScopes{ 0 };
        std::atomic<std::uint64_t> m_staleEvents{ 0 };
        mutable std::atomic<std::uint64_t> m_foreignTouches{ 0 };
        const std::thread::id m_ownerThread = std::this_thread::get_id();
    };
}
