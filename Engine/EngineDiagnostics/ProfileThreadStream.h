#pragma once
// PHASE 14 P2 — 스레드 스트림과 청크 풀.
//
// 소유권이 이 파일의 전부다:
//
//   chunk_pool   free 청크를 나눠 주고 되받는다. 봉인된 청크의 유일한 주인.
//   thread_stream  writer 하나가 쓰는 현재 청크와 열린 스코프 스택.
//
// writer 는 자기 thread_stream 만 만지고, 수집기는 chunk_pool 의 sealed 목록만
// 만진다. 옛 코어가 스레드 표를 순회하며 남의 TLS 를 읽던 자리가 여기서
// 사라진다 — 수집기는 스레드를 순회하지 않는다.
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ProfileEvent.h"

namespace ce
{
	// 한 스레드가 프로파일러에 보이는 이름과 자리.
	struct thread_info
	{
		std::string   name;
		std::uint32_t os_thread_id = 0;
		std::uint32_t slot = 0;
	};

	// 청크를 나눠 주고 봉인된 것을 모은다. free 가 없으면 **막지 않고**
	// drop 을 센다(§6.2) — 관측 도구가 관측 대상을 멈춰 세우면 그 수치는
	// 이미 관측이 아니다.
	class chunk_pool
	{
	public:
		chunk_pool() = default;
		~chunk_pool();

		chunk_pool(const chunk_pool&) = delete;
		chunk_pool& operator=(const chunk_pool&) = delete;

		void initialize(std::uint32_t chunk_count);
		void shutdown();

		// free 목록에서 하나 꺼낸다. 비어 있으면 nullptr — 호출자가 drop 을
		// 센다. 절대 기다리지 않는다.
		event_chunk* acquire();

		// writer 가 다 쓴 청크를 넘긴다. 이 호출 뒤 writer 는 그 포인터를
		// 다시 쓰지 않는다.
		void seal(event_chunk* chunk);

		// 봉인된 목록을 통째로 떼어 온다. 수집기만 부른다.
		event_chunk* take_sealed();

		// 수집이 끝난 청크를 free 로 되돌린다.
		void release(event_chunk* chunk_list);

		std::uint32_t chunk_count() const { return m_chunkCount; }
		std::uint32_t free_count() const;

	private:
		std::vector<std::unique_ptr<event_chunk>> m_storage;

		mutable std::mutex m_lock;
		event_chunk*  m_free = nullptr;
		event_chunk*  m_sealed = nullptr;
		event_chunk*  m_sealedTail = nullptr;
		std::uint32_t m_chunkCount = 0;
		std::uint32_t m_freeCount = 0;
	};

	// 열린 스코프 하나. 깊이와 시작 시각을 들고 있다가 닫힐 때 이벤트가 된다.
	struct open_scope
	{
		profile_tick  tick_begin = 0;
		marker_id     marker = invalid_marker;
		std::uint32_t frame = 0;
		event_flags   flags = event_flags::none;

		// 이 구간을 연 녹화 세대. Clear 는 세대를 올리므로, 지우기 전에 열린
		// 구간은 나중에 닫혀도 새 캡처의 것이 아니다.
		//
		// ★ 세대를 청크에만 찍으면 이것을 못 막는다. 스코프는 청크보다 오래
		//   산다 — 청크는 프레임마다 봉인되지만 구간은 그것을 넘어 열려 있다.
		std::uint64_t generation = 0;

		// 얼림에서 이미 잘려 기록됐다. 진짜 종료가 와도 다시 적지 않는다.
		//
		// ★ 예전에는 이것을 개수(m_skippedDepth)로만 예약했다. 그런데 깊이
		//   상한으로 못 연 구간은 언제나 가장 **안쪽**인 반면 잘린 구간은 가장
		//   **바깥**이라, 개수로 세면 다시 녹화한 뒤의 새 구간의 종료가 그
		//   예약을 먼저 먹고 자기는 열린 채 남았다. 짝은 자리로 맞춰야 한다.
		bool          emitted = false;
	};

	// 스코프 스택 상한. 넘으면 **버리고 센다** — 옛 코어는 넘긴 Push 가
	// TLS 의 다음 멤버를 덮어썼고, NDEBUG 에서는 assert 가 사라져 조용히
	// 힙을 망가뜨렸다(결함 6).
	inline constexpr std::uint32_t kMaxScopeDepth = 64;

	// writer 하나가 소유한다. 이 타입의 어떤 멤버도 다른 스레드가 쓰지 않는다.
	class thread_stream
	{
	public:
		thread_stream(chunk_pool& pool, thread_info info);
		~thread_stream();

		thread_stream(const thread_stream&) = delete;
		thread_stream& operator=(const thread_stream&) = delete;

		void begin_scope(marker_id id, profile_tick now, std::uint32_t frame);
		void end_scope(profile_tick now);

		// 열지 않은 스코프의 짝을 예약한다. 녹화 중이 아니어서 여는 쪽을 건너뛰었을
		// 때 쓴다 — 여는 쪽만 건너뛰면 그 짝이 스택에서 **남의 구간을 닫는다.**
		// 깊이 상한을 넘겨 못 열었을 때와 정확히 같은 기제다(begin_scope 의 주석).
		//
		// ★ 버렸다고 세지는 않는다. 이것은 용량 부족으로 **잃은** 것이 아니라
		//   녹화하지 않기로 해서 안 재는 것이다. 들어서 세면 pause 를 누를 때마다
		//   손실 계수기가 오른다.
		void skip_scope()
		{
			// 얼어 있는 동안에도 이 자리는 지난다. 여기서 요청을 보지 않으면
			// 워커는 다음 녹화까지 얼림을 모른다 — pause 가 여는 쪽을 막으므로
			// write() 를 더는 타지 않기 때문이다.
			honor_seal_request();
			++m_skippedDepth;
		}

		// 이미 끝난 구간을 그대로 적는다. 스코프 스택을 쓰지 않는다 —
		// GPU 구간은 나중에, 완성된 채로, 시작 시각까지 들고 온다.
		//
		// ★ 틱은 **CPU(QPC) 축으로 옮긴 뒤**의 값이어야 한다. 이 층은 GPU 틱을
		//   모르고, 옮기는 일은 두 시계를 가진 백엔드의 몫이다.
		void write_span(marker_id id, profile_tick begin, profile_tick end,
		                std::uint32_t frame, std::uint16_t depth);

		// 프레임 경계. 열려 있는 스코프는 닫지 않는다 — 그것이 프레임을 넘는
		// 구간이고, 옛 코어가 스택 맨 위를 무조건 닫아 잃던 것이다. 대신
		// 지금까지 쓴 청크를 봉인해 수집기가 이번 프레임을 볼 수 있게 한다.
		//
		// ★ **주인 스레드만 부른다.** 남이 부르면 m_writer 를 비우는 동안 주인이
		//   그 포인터로 쓰고 있을 수 있다. 실측: 워커 넷이 적는 동안 수집기가
		//   이 함수를 돌리자 Debug·Release 모두 ACCESS_VIOLATION 으로 죽었다.
		void publish_frame();

		// 수집기가 "지금 봉인해 달라" 고 **요청**만 한다. 실제 봉인은 주인
		// 스레드가 자기 안전한 자리에서 한다.
		//
		// ★ 이것이 경계의 전부다. 수집기는 청크 포인터를 만지지 않는다.
		// 열려 있는 구간을 그 시각에서 잘라 기록한다. 짝은 예약해 두므로
		// 나중에 실제 end_scope 가 와도 **두 번 기록하지 않고 남의 구간도
		// 닫지 않는다** — 깊이 상한을 넘겼을 때와 같은 기제다.
		void truncate_open_scopes(profile_tick freeze_tick);

		void request_seal()
		{
			m_sealRequest.fetch_add(1, std::memory_order_release);
		}

		// 봉인에 더해 **열려 있는 구간을 그 시각에서 잘라 달라**고 요청한다.
		// pause 가 쓴다 — 얼린 캡처에는 다음 프레임이 없으므로, 여기서 남기지
		// 않으면 그 구간은 영영 사라진다(§6.1).
		void request_freeze(profile_tick freeze_tick)
		{
			m_freezeTick.store(freeze_tick, std::memory_order_release);
			m_sealRequest.fetch_add(1, std::memory_order_release);
		}

		// 이 스트림이 쓰는 녹화 세대. 수집기가 올리면 다음 청크부터 새 세대다.
		void set_generation(std::uint64_t value)
		{
			m_generation.store(value, std::memory_order_release);
		}

		// 요청한 봉인이 처리됐는가. 수집기가 청크를 만지지 않고 물을 수 있는
		// 유일한 수단이다.
		std::uint64_t seal_request() const
		{
			return m_sealRequest.load(std::memory_order_acquire);
		}
		std::uint64_t seal_ack() const
		{
			return m_sealAck.load(std::memory_order_acquire);
		}

		// 이 스레드의 기록을 끝낸다. 남은 청크를 봉인하고, 아직 열려 있는
		// 스코프가 있으면 truncated_end 로 닫아 **잃지 않는다**.
		void finish(profile_tick now);

		const thread_info& info() const { return m_info; }
		std::uint32_t      slot() const { return m_info.slot; }
		std::uint32_t      open_depth() const { return m_depth; }

		// ★ 원자로 둔다. 적는 것은 주인 스레드이고 읽는 것은 수집기·요약이라,
		//   평범한 정수면 그 자체로 경합이다(계획서 §0.5.15).
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

		// 지운 세대에서 열려 닫힐 때 버려진 구간 수.
		std::uint64_t stale_scopes() const
		{
			return m_staleScopes.load(std::memory_order_relaxed);
		}

	private:
		bool ensure_chunk();
		void write(const profile_event& value);
		void seal_current();

		// 요청이 와 있으면 지금 봉인한다. 주인 스레드의 안전한 자리에서만
		// 불린다 — write() 가 청크를 만지기 **전**과 스코프가 다 닫힌 뒤다.
		void honor_seal_request();

		chunk_pool&   m_pool;
		thread_info   m_info;

		event_chunk*  m_writer = nullptr;
		std::uint64_t m_sequence = 0;

		open_scope    m_stack[kMaxScopeDepth]{};
		std::uint32_t m_depth = 0;

		// 깊이 상한을 넘겨 열지 못한 스코프 수. 그 스코프들도 닫히므로,
		// end_scope 가 스택을 건드리기 전에 이 수를 먼저 소비해야 짝이 맞는다.
		std::uint32_t m_skippedDepth = 0;

		// 수집기가 올리고 주인이 따라 올린다. 둘이 같으면 요청이 다 처리된 것이다.
		std::atomic<std::uint64_t> m_sealRequest{ 0 };
		std::atomic<std::uint64_t> m_sealAck{ 0 };
		std::atomic<profile_tick>  m_freezeTick{ 0 };

		// 봉인 처리 중인가. 주인 스레드만 읽고 쓴다.
		bool m_inHonor = false;
		std::atomic<std::uint64_t> m_generation{ 0 };

		std::atomic<std::uint64_t> m_droppedEvents{ 0 };    // free 청크가 없어 잃은 이벤트
		std::atomic<std::uint64_t> m_droppedScopes{ 0 };    // 깊이 상한을 넘겨 못 연 스코프
		std::atomic<std::uint64_t> m_unbalancedScopes{ 0 };
		std::atomic<std::uint64_t> m_staleScopes{ 0 }; // 열지 않고 닫은 횟수
	};
}
