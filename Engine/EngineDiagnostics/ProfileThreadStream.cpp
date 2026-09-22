#include "ProfileThreadStream.h"

#include <utility>

namespace ce
{
	bool track_precedes(const thread_info& a, const thread_info& b)
	{
		if (a.kind != b.kind) return a.kind < b.kind;
		if (a.track_order != b.track_order) return a.track_order < b.track_order;
		return a.slot < b.slot;
	}

	//-------------------------------------------------------------------------
	// chunk_pool
	//-------------------------------------------------------------------------

	chunk_pool::~chunk_pool()
	{
		shutdown();
	}

	void chunk_pool::initialize(std::uint32_t chunk_count)
	{
		std::lock_guard<std::mutex> guard(m_lock);

		m_storage.clear();
		m_storage.reserve(chunk_count);
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_chunkCount = chunk_count;
		m_freeCount = chunk_count;

		// 한 번에 다 잡아 둔다. hot path 에서 heap 을 건드리지 않는 것이
		// 계약이므로(§6.2) 이후의 할당은 존재하지 않는다 — 옛 코어의
		// BeginEvent 가 버퍼를 resize 하던 자리가 여기서 사라진다.
		for (std::uint32_t i = 0; i < chunk_count; ++i)
		{
			auto chunk = std::make_unique<event_chunk>();
			chunk->next = m_free;
			m_free = chunk.get();
			m_storage.push_back(std::move(chunk));
		}
	}

	void chunk_pool::shutdown()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		m_free = nullptr;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		m_freeCount = 0;
		m_chunkCount = 0;
		m_storage.clear();
	}

	event_chunk* chunk_pool::acquire()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		if (!m_free)
		{
			return nullptr;
		}

		event_chunk* chunk = m_free;
		m_free = chunk->next;
		chunk->next = nullptr;
		--m_freeCount;
		return chunk;
	}

	void chunk_pool::seal(event_chunk* chunk)
	{
		if (!chunk)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_lock);

		// 꼬리에 붙인다. 청크 안의 순서는 writer 가, 청크 사이의 순서는
		// 이 목록이 지킨다.
		chunk->next = nullptr;
		if (m_sealedTail)
		{
			m_sealedTail->next = chunk;
			m_sealedTail = chunk;
		}
		else
		{
			m_sealed = chunk;
			m_sealedTail = chunk;
		}
	}

	event_chunk* chunk_pool::take_sealed()
	{
		std::lock_guard<std::mutex> guard(m_lock);
		event_chunk* head = m_sealed;
		m_sealed = nullptr;
		m_sealedTail = nullptr;
		return head;
	}

	void chunk_pool::release(event_chunk* chunk_list)
	{
		if (!chunk_list)
		{
			return;
		}

		std::lock_guard<std::mutex> guard(m_lock);
		while (chunk_list)
		{
			event_chunk* next = chunk_list->next;
			chunk_list->count = 0;
			chunk_list->next = m_free;
			m_free = chunk_list;
			++m_freeCount;
			chunk_list = next;
		}
	}

	std::uint32_t chunk_pool::free_count() const
	{
		std::lock_guard<std::mutex> guard(m_lock);
		return m_freeCount;
	}

	//-------------------------------------------------------------------------
	// thread_stream
	//-------------------------------------------------------------------------

	thread_stream::thread_stream(chunk_pool& pool, thread_info info)
		: m_pool(pool)
		, m_info(std::move(info))
	{
	}

	thread_stream::~thread_stream()
	{
		// ★ 주인이 아니면 **아무것도 만지지 않는다.** 남의 finish() 를 피해도
		//   소멸자가 소유 검사 없이 seal_current() 를 부르면 같은 일이다 —
		//   주인이 m_writer 로 쓰는 동안 그 포인터를 비우고 청크를 되돌린다.
		//   여기까지 온 것은 호출자가 파괴해도 된다고 판단한 것이지만, 그
		//   판단이 틀렸을 때 죽는 대신 청크 하나를 잃는 쪽을 고른다.
		if (!owned_by_caller())
		{
			return;
		}

		// 남은 청크를 잃지 않는다. 열린 스코프는 finish() 가 닫았어야 하지만,
		// 안 불렸더라도 여기서 청크는 넘어간다.
		seal_current();
	}

	bool thread_stream::ensure_chunk()
	{
		if (m_writer && !m_writer->full())
		{
			return true;
		}

		if (m_writer)
		{
			seal_current();
		}

		m_writer = m_pool.acquire();
		if (!m_writer)
		{
			return false;
		}

		m_writer->reset(m_info.slot, m_sequence);
		m_writer->generation = m_generation.load(std::memory_order_acquire);
		m_pendingWork.store(true, std::memory_order_release);
		return true;
	}

	void thread_stream::honor_seal_request()
	{
		// ★ 자르는 동안 write() 가 다시 여기로 들어온다. 그 안쪽 호출이 먼저
		//   ack 를 올리면, 아직 청크에 들어가지도 않은 잘린 구간을 두고 수집기가
		//   "다 봉인됐다" 로 읽고 거둬 가 버린다 — 실측에서 pause-worker/present
		//   가 간헐로 붉었던 이유다. ack 는 **다 끝난 뒤** 한 번만 올린다.
		if (m_inHonor)
		{
			return;
		}

		const std::uint64_t requested = m_sealRequest.load(std::memory_order_acquire);
		if (requested == m_sealAck.load(std::memory_order_relaxed))
		{
			return;
		}

		m_inHonor = true;

		// ★ 얼림 요청이면 열린 구간을 그 시각에서 잘라 **남긴다.** 평범한
		//   프레임 경계에서는 건드리지 않는다 — 그것이 프레임을 넘는 구간이다.
		const profile_tick freeze = m_freezeTick.exchange(0, std::memory_order_acq_rel);
		if (freeze != 0)
		{
			truncate_open_scopes(freeze);
		}

		seal_current();
		m_sealAck.store(requested, std::memory_order_release);
		if (freeze != 0)
		{
			m_freezeAck.store(m_freezeRequest.load(std::memory_order_acquire),
			                  std::memory_order_release);
		}
		m_inHonor = false;
	}

	void thread_stream::write(const profile_event& value)
	{
		if (!owned_by_caller()) return;

		// 수집기가 봉인을 청했으면 **여기서** 들어준다. 청크를 만지기 전이
		// 유일하게 안전한 자리다 — 그리고 이 스레드만 여기를 지난다.
		honor_seal_request();

		if (!ensure_chunk())
		{
			m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		m_writer->events[m_writer->count] = value;
		m_writer->events[m_writer->count].thread_slot =
			static_cast<std::uint16_t>(m_info.slot);
		++m_writer->count;
		++m_sequence;
	}

	bool thread_stream::owned_by_caller() const
	{
		// thread_local 에 한 번 담아 두고 비교한다. 소유 검사가 hot path 에
		// 걸리므로 매번 물어보지 않는다.
		static thread_local const std::thread::id self = std::this_thread::get_id();
		if (m_ownerThread == self)
		{
			return true;
		}
		m_foreignTouches.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	void thread_stream::seal_current()
	{
		// 넘길 것이 남았는가를 여기서 다시 센다. 열린 구간이 없고 쓰던 청크도
		// 없으면 이 스트림은 얼림에 응답할 것이 없다.
		m_pendingWork.store(m_depth > 0, std::memory_order_release);

		if (!m_writer)
		{
			return;
		}

		event_chunk* chunk = m_writer;
		m_writer = nullptr;

		if (chunk->count == 0)
		{
			// 빈 청크는 수집기에 보내지 않는다 — 봉인 목록에 빈 것이 섞이면
			// "이 프레임에 아무 일도 없었다" 와 "아직 안 왔다" 가 구분되지 않는다.
			m_pool.release(chunk);
			return;
		}

		m_pool.seal(chunk);
	}

	void thread_stream::begin_scope(marker_id id, profile_tick now, std::uint32_t frame)
	{
		if (!owned_by_caller()) return;
		honor_seal_request();

		if (m_depth >= kMaxScopeDepth)
		{
			// 버리고 센다. 옛 코어는 이 자리에서 스택 밖에 썼다.
			//
			// ★ 못 연 것을 따로 세어 두는 것이 핵심이다. 이 스코프도 언젠가
			//   닫히는데, 그때 스택에서 하나 꺼내면 **남의 구간을 닫는다** —
			//   RAII 표기라도 짝이 어긋난다. 스코프는 LIFO 이므로 못 연 것이
			//   언제나 가장 안쪽이고, 따라서 end_scope 가 이 수를 먼저 소비하면
			//   정확히 제 짝을 만난다.
			++m_skippedDepth;
			m_droppedScopes.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		m_pendingWork.store(true, std::memory_order_release);

		open_scope& scope = m_stack[m_depth];
		scope.tick_begin = now;
		scope.marker = id;
		scope.frame = frame;
		scope.flags = event_flags::none;
		scope.generation = m_generation.load(std::memory_order_acquire);
		scope.emitted = false;
		++m_depth;
	}

	void thread_stream::end_scope(profile_tick now)
	{
		if (!owned_by_caller()) return;

		// ★ 짝을 보기 **전에** 요청을 들어준다. 얼림이면 지금 닫으려는 구간도
		//   잘려 나가고 짝이 예약되므로, 바로 아래에서 그 예약을 소비한다 —
		//   한 구간이 두 번 적히지 않는다.
		honor_seal_request();

		// 깊이 상한을 넘겨 못 연 스코프의 짝이 먼저다(begin_scope 의 주석).
		// 이것을 스택보다 먼저 보지 않으면 남의 구간을 닫는다.
		if (m_skippedDepth > 0)
		{
			--m_skippedDepth;
			return;
		}

		if (m_depth == 0)
		{
			// 열지 않고 닫았다. RAII 표기에서는 나올 수 없는 경로다 —
			// 남는다면 손으로 짝을 맞춘 코드가 어딘가 있다는 뜻이므로 센다.
			m_unbalancedScopes.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		--m_depth;
		const open_scope& scope = m_stack[m_depth];

		// 얼림에서 이미 잘려 나갔다. 여기서 또 적으면 한 구간이 두 번 남는다.
		if (scope.emitted)
		{
			return;
		}

		// 지운 세대에서 연 구간이다. 지금 캡처의 것이 아니므로 적지 않고 센다 —
		// 조용히 섞으면 지운 것이 돌아온 것처럼 보인다.
		if (scope.generation != m_generation.load(std::memory_order_acquire))
		{
			m_staleScopes.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		profile_event value;
		value.tick_begin = scope.tick_begin;
		value.tick_end = now;
		value.marker = scope.marker;
		value.frame = scope.frame;
		value.depth = static_cast<std::uint16_t>(m_depth);
		value.flags = scope.flags;
		write(value);
	}

	void thread_stream::write_span(marker_id id, profile_tick begin, profile_tick end,
	                               std::uint32_t frame, std::uint16_t depth,
	                               const gpu_span_context& gpu)
	{
		if (!owned_by_caller()) return;

		if (!ensure_chunk())
		{
			m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		// 이 청크는 늦은 귀속을 거친다. 첫 이벤트를 쓸 때 세워 두면 청크가
		// 재사용되어도 다음 acquire 에서 reset 이 다시 내린다.
		m_writer->late_ingest = true;

		profile_event value;
		value.tick_begin = begin;
		value.tick_end = end;
		value.marker = id;
		value.frame = frame;
		value.depth = depth;
		value.flags = event_flags::gpu_span;
		value.submission = gpu.submission;
		value.view = gpu.view;
		value.queue = gpu.queue;
		write(value);
	}

	void thread_stream::write_instant(marker_id id, profile_tick tick,
	                                  std::uint32_t frame)
	{
		if (!owned_by_caller()) return;

		// 얼림·봉인 요청을 여기서도 본다. 드물게 오는 사건이라 이 자리가
		// 한 스레드의 유일한 안전한 자리일 수 있다.
		honor_seal_request();

		if (!ensure_chunk())
		{
			m_droppedEvents.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		profile_event value;
		value.tick_begin = tick;
		value.tick_end = tick;
		value.marker = id;
		value.frame = frame;
		value.depth = static_cast<std::uint16_t>(m_depth);
		value.flags = event_flags::instant;
		write(value);
	}

	void thread_stream::publish_frame()
	{
		// ★ 남이 부르면 아무것도 하지 않는다. 예전에는 수집기가 여기로 들어와
		//   남의 m_writer 를 비웠고, 그 포인터로 쓰고 있던 주인과 겹쳐 죽었다.
		if (!owned_by_caller()) return;

		// ★ 열린 스코프를 닫지 않는다. 프레임을 넘는 구간은 닫히는 프레임에
		//   기록되고, 시작 프레임 번호를 들고 있으므로 분석기가 어느 프레임에
		//   걸쳐 있었는지 복원할 수 있다. 옛 코어는 여기서 스택 맨 위를 무조건
		//   닫았고(그래서 "CPU Frame" 대신 남의 구간을 닫았다) 그것이
		//   cross-frame/preserve 가 기지 결함으로 남아 있던 이유다.
		seal_current();

		// 주인이 직접 봉인했으니 대기 중인 요청도 함께 풀린 것이다.
		m_sealAck.store(m_sealRequest.load(std::memory_order_acquire),
		                std::memory_order_release);
	}

	void thread_stream::freeze_self(profile_tick freeze_tick)
	{
		if (!owned_by_caller()) return;

		truncate_open_scopes(freeze_tick);
		seal_current();
		m_sealAck.store(m_sealRequest.load(std::memory_order_acquire),
		                std::memory_order_release);
		m_freezeAck.store(m_freezeRequest.load(std::memory_order_acquire),
		                  std::memory_order_release);
	}

	void thread_stream::truncate_open_scopes(profile_tick freeze_tick)
	{
		if (!owned_by_caller()) return;

		// ★ 스택에서 **꺼내지 않는다.** 그 구간은 아직 열려 있고, 여기서 자리를
		//   비우면 다시 녹화한 뒤의 새 구간이 그 자리를 차지해 짝이 뒤집힌다.
		//   대신 '이미 적었다' 고 표시해 두고, 진짜 종료가 올 때 그 자리에서
		//   확인한다 — 짝은 개수가 아니라 자리로 맞춘다.
		const std::uint64_t generation = m_generation.load(std::memory_order_acquire);

		for (std::uint32_t i = m_depth; i > 0; --i)
		{
			open_scope& scope = m_stack[i - 1];
			if (scope.emitted) continue;
			if (scope.generation != generation) continue;

			profile_event value;
			value.tick_begin = scope.tick_begin;
			value.tick_end = freeze_tick;
			value.marker = scope.marker;
			value.frame = scope.frame;
			value.depth = static_cast<std::uint16_t>(i - 1);
			value.flags = scope.flags | event_flags::truncated_end;
			write(value);

			scope.emitted = true;
		}
	}

	void thread_stream::finish(profile_tick now)
	{
		if (!owned_by_caller()) return;

		// 아직 열려 있는 것을 잃지 않고 닫는다 — 끝을 못 본 구간이라고 표시해서.
		const std::uint64_t generation = m_generation.load(std::memory_order_acquire);

		while (m_depth > 0)
		{
			--m_depth;
			const open_scope& scope = m_stack[m_depth];

			// 얼림에서 이미 적었거나 지운 세대의 것이면 다시 적지 않는다.
			if (scope.emitted) continue;
			if (scope.generation != generation)
			{
				m_staleScopes.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			profile_event value;
			value.tick_begin = scope.tick_begin;
			value.tick_end = now;
			value.marker = scope.marker;
			value.frame = scope.frame;
			value.depth = static_cast<std::uint16_t>(m_depth);
			value.flags = scope.flags | event_flags::truncated_end;
			write(value);

			m_unbalancedScopes.fetch_add(1, std::memory_order_relaxed);
		}

		seal_current();
	}
}
