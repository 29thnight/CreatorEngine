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
		void skip_scope() { ++m_skippedDepth; }

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
		void publish_frame();

		// 이 스레드의 기록을 끝낸다. 남은 청크를 봉인하고, 아직 열려 있는
		// 스코프가 있으면 truncated_end 로 닫아 **잃지 않는다**.
		void finish(profile_tick now);

		const thread_info& info() const { return m_info; }
		std::uint32_t      slot() const { return m_info.slot; }
		std::uint32_t      open_depth() const { return m_depth; }

		std::uint64_t dropped_events() const { return m_droppedEvents; }
		std::uint64_t dropped_scopes() const { return m_droppedScopes; }
		std::uint64_t unbalanced_scopes() const { return m_unbalancedScopes; }

	private:
		bool ensure_chunk();
		void write(const profile_event& value);
		void seal_current();

		chunk_pool&   m_pool;
		thread_info   m_info;

		event_chunk*  m_writer = nullptr;
		std::uint64_t m_sequence = 0;

		open_scope    m_stack[kMaxScopeDepth]{};
		std::uint32_t m_depth = 0;

		// 깊이 상한을 넘겨 열지 못한 스코프 수. 그 스코프들도 닫히므로,
		// end_scope 가 스택을 건드리기 전에 이 수를 먼저 소비해야 짝이 맞는다.
		std::uint32_t m_skippedDepth = 0;

		std::uint64_t m_droppedEvents = 0;    // free 청크가 없어 잃은 이벤트
		std::uint64_t m_droppedScopes = 0;    // 깊이 상한을 넘겨 못 연 스코프
		std::uint64_t m_unbalancedScopes = 0; // 열지 않고 닫은 횟수
	};
}
